// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ComfyUI-DLSS5-NR contributors
// Adapted for obs-dlss5-nr (GPL-2.0 project) by Saganaki22, 2026.
//
// NVIDIA Optical Flow D3D11 integration.
//
// Follows NVIDIA's public NVOF programming guide: capability probing before
// initialization, supported surface-format queries during buffer allocation.
// Uses the API-2.0 ABI declarations; no NVIDIA SDK headers are vendored.
//
// Input frames arrive as 8-bit BGRA rows (OBS native format); luminance is
// derived with the Rec.709 coefficients, matching the reference project.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "nvof_flow.h"

using Microsoft::WRL::ComPtr;

namespace {

using NvOfStatus = long;
static constexpr NvOfStatus kOfSuccess = 0;
static constexpr NvOfStatus kOfRaised = 0x7FFFFFFF;
static constexpr uint32_t kOfApiVersion = 0x20; // public 2.0 ABI layout
static constexpr uint32_t kPreferredGrid = 2;
static constexpr uint32_t kPerf = 20; // NV_OF_PERF_LEVEL_FAST

static constexpr uint32_t kModeOpticalFlow = 1;
static constexpr uint32_t kUsageInput = 1;
static constexpr uint32_t kUsageOutput = 2;
static constexpr uint32_t kCapsOutputGridSizes = 0;

struct NvOfInitParams {
	uint32_t width;
	uint32_t height;
	uint32_t outGridSize;
	uint32_t hintGridSize;
	uint32_t mode;
	uint32_t perfLevel;
	uint32_t enableExternalHints;
	uint32_t enableOutputCost;
	void *hPrivData;
	uint32_t disparityRange;
	uint32_t enableRoi;
};

struct NvOfExecuteInputParams {
	void *inputFrame;
	void *referenceFrame;
	void *externalHints;
	uint32_t disableTemporalHints;
	uint32_t padding;
	void *hPrivData;
	uint32_t padding2;
	uint32_t numRois;
	void *roiData;
};

struct NvOfExecuteOutputParams {
	void *outputBuffer;
	void *outputCostBuffer;
	void *hPrivData;
};

static_assert(sizeof(NvOfInitParams) == 48, "Unexpected NVOF init ABI layout");
static_assert(offsetof(NvOfInitParams, hPrivData) == 32, "Unexpected NVOF init ABI layout");
static_assert(sizeof(NvOfExecuteInputParams) == 56, "Unexpected NVOF execute ABI layout");
static_assert(offsetof(NvOfExecuteInputParams, numRois) == 44, "Unexpected NVOF execute ABI layout");

using PFN_CreateInstance = NvOfStatus(__stdcall *)(uint32_t, void *);
using PFN_CreateD3D11 = NvOfStatus(__stdcall *)(ID3D11Device *, ID3D11DeviceContext *, void **);
using PFN_Init = NvOfStatus(__stdcall *)(void *, const NvOfInitParams *);
using PFN_GetSurfaceFormatCount = NvOfStatus(__stdcall *)(void *, uint32_t, uint32_t, uint32_t *);
using PFN_GetSurfaceFormat = NvOfStatus(__stdcall *)(void *, uint32_t, uint32_t, DXGI_FORMAT *);
using PFN_Register = NvOfStatus(__stdcall *)(void *, ID3D11Resource *, void **);
using PFN_Unregister = NvOfStatus(__stdcall *)(void *);
using PFN_Execute = NvOfStatus(__stdcall *)(void *, const NvOfExecuteInputParams *, NvOfExecuteOutputParams *);
using PFN_Destroy = NvOfStatus(__stdcall *)(void *);
using PFN_LastError = NvOfStatus(__stdcall *)(void *, char *, uint32_t *);
using PFN_GetCaps = NvOfStatus(__stdcall *)(void *, uint32_t, uint32_t *, uint32_t *);

// API 2.0 D3D11 function table order used by NVIDIA's public interface:
//  0 CreateOpticalFlowD3D11
//  1 NvOFInit
//  2 NvOFGetSurfaceFormatCountD3D11
//  3 NvOFGetSurfaceFormatD3D11
//  4 NvOFRegisterResourceD3D11
//  5 NvOFUnregisterResourceD3D11
//  6 NvOFExecute
//  7 NvOFDestroy
//  8 NvOFGetLastError
//  9 NvOFGetCaps
struct OfaState {
	HMODULE lib = nullptr;
	void *slot[64]{};
	void *session = nullptr;
	void *reg_src[2]{};
	void *reg_flow = nullptr;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11Texture2D> src[2];
	ComPtr<ID3D11Texture2D> flow;
	ComPtr<ID3D11Texture2D> flow_staging;

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t flow_width = 0;
	uint32_t flow_height = 0;
	uint32_t selected_grid = kPreferredGrid;
	DXGI_FORMAT input_format = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT output_format = DXGI_FORMAT_UNKNOWN;
	int current = 0;
	bool primed = false;

	LUID adapter_luid{};
	bool have_luid = false;
	std::vector<uint8_t> luma_upload;

	std::vector<uint32_t> supported_grids;
	std::vector<DXGI_FORMAT> supported_input_formats;
	std::vector<DXGI_FORMAT> supported_output_formats;
	std::string last_driver_error;
	uint32_t last_status = 0;
	std::string diagnostics = "NVOF has not been probed yet.";
};

static OfaState g_ofa;

static uint32_t StatusBits(NvOfStatus s)
{
	return static_cast<uint32_t>(s);
}

static const char *StatusName(NvOfStatus s)
{
	switch (StatusBits(s)) {
	case 0:
		return "SUCCESS";
	case 1:
		return "OF_NOT_AVAILABLE";
	case 2:
		return "UNSUPPORTED_DEVICE";
	case 3:
		return "DEVICE_DOES_NOT_EXIST";
	case 4:
		return "INVALID_PTR";
	case 5:
		return "INVALID_PARAM";
	case 6:
		return "INVALID_CALL";
	case 7:
		return "INVALID_VERSION";
	case 8:
		return "OUT_OF_MEMORY";
	case 9:
		return "NOT_INITIALIZED";
	case 10:
		return "UNSUPPORTED_FEATURE";
	case 11:
		return "GENERIC";
	case 0x80004005u:
		return "E_FAIL (driver HRESULT)";
	case 0x80004003u:
		return "E_POINTER (driver HRESULT)";
	case 0x8007000Eu:
		return "E_OUTOFMEMORY (driver HRESULT)";
	case 0x80070057u:
		return "E_INVALIDARG (driver HRESULT)";
	default:
		return "UNKNOWN_STATUS";
	}
}

static const char *DxgiFormatName(DXGI_FORMAT f)
{
	switch (f) {
	case DXGI_FORMAT_R8_UNORM:
		return "R8_UNORM";
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return "R8G8B8A8_UNORM";
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return "B8G8R8A8_UNORM";
	case DXGI_FORMAT_R16G16_SINT:
		return "R16G16_SINT";
	case DXGI_FORMAT_R16G16_FLOAT:
		return "R16G16_FLOAT";
	default:
		return nullptr;
	}
}

static std::string FormatName(DXGI_FORMAT f)
{
	if (const char *n = DxgiFormatName(f))
		return n;
	char buf[48];
	std::snprintf(buf, sizeof(buf), "DXGI_FORMAT_%u", static_cast<unsigned>(f));
	return buf;
}

static std::string FormatList(const std::vector<DXGI_FORMAT> &values)
{
	if (values.empty())
		return "<unavailable>";
	std::ostringstream os;
	for (size_t i = 0; i < values.size(); ++i) {
		if (i)
			os << ", ";
		os << FormatName(values[i]);
	}
	return os.str();
}

static void RefreshDiagnostics(const char *phase = nullptr)
{
	std::ostringstream os;
	os << "Supported output grids:";
	for (uint32_t g : g_ofa.supported_grids)
		os << " " << g;
	os << "\nSelected grid: " << g_ofa.selected_grid << "\n";
	os << "Supported D3D11 input formats: " << FormatList(g_ofa.supported_input_formats) << "\n";
	os << "Supported D3D11 output formats: " << FormatList(g_ofa.supported_output_formats) << "\n";
	os << "Selected input format: " << FormatName(g_ofa.input_format) << "\n";
	os << "Selected output format: " << FormatName(g_ofa.output_format) << "\n";
	if (g_ofa.width && g_ofa.height)
		os << "Session resolution: " << g_ofa.width << "x" << g_ofa.height << "\n";
	if (phase && *phase)
		os << "Last phase: " << phase << "\n";
	if (g_ofa.last_status)
		os << "Last NVOF status: 0x" << std::hex << StatusBits(g_ofa.last_status) << std::dec << " ("
		   << StatusName(static_cast<NvOfStatus>(g_ofa.last_status)) << ")\n";
	if (!g_ofa.last_driver_error.empty())
		os << "Driver detail: " << g_ofa.last_driver_error << "\n";
	g_ofa.diagnostics = os.str();
}

// Guard all calls crossing the dynamically described ABI; useful while
// supporting multiple driver generations without vendoring SDK headers.
static NvOfStatus SafeCreateInstance(PFN_CreateInstance fn, uint32_t version, void *slots)
{
	__try {
		return fn(version, slots);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeCreateSession(PFN_CreateD3D11 fn, ID3D11Device *d, ID3D11DeviceContext *c, void **out)
{
	__try {
		return fn(d, c, out);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeInit(PFN_Init fn, void *session, const NvOfInitParams *params)
{
	__try {
		return fn(session, params);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeGetSurfaceFormatCount(PFN_GetSurfaceFormatCount fn, void *session, uint32_t usage, uint32_t mode,
					    uint32_t *count)
{
	__try {
		return fn(session, usage, mode, count);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeGetSurfaceFormat(PFN_GetSurfaceFormat fn, void *session, uint32_t usage, uint32_t mode,
				       DXGI_FORMAT *formats)
{
	__try {
		return fn(session, usage, mode, formats);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeRegister(PFN_Register fn, void *session, ID3D11Resource *resource, void **out)
{
	__try {
		return fn(session, resource, out);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeUnregister(PFN_Unregister fn, void *handle)
{
	__try {
		return fn(handle);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeExecute(PFN_Execute fn, void *session, const NvOfExecuteInputParams *in,
			      NvOfExecuteOutputParams *out)
{
	__try {
		return fn(session, in, out);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeDestroy(PFN_Destroy fn, void *session)
{
	__try {
		return fn(session);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeLastError(PFN_LastError fn, void *session, char *buf, uint32_t *cap)
{
	__try {
		return fn(session, buf, cap);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}
static NvOfStatus SafeGetCaps(PFN_GetCaps fn, void *session, uint32_t cap, uint32_t *values, uint32_t *size)
{
	__try {
		return fn(session, cap, values, size);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return kOfRaised;
	}
}

static std::string LastError()
{
	if (!g_ofa.session || !g_ofa.slot[8])
		return {};
	char buf[1024]{};
	uint32_t cap = static_cast<uint32_t>(sizeof(buf));
	auto fn = reinterpret_cast<PFN_LastError>(g_ofa.slot[8]);
	const NvOfStatus r = SafeLastError(fn, g_ofa.session, buf, &cap);
	if (r == kOfRaised)
		buf[0] = '\0';
	buf[sizeof(buf) - 1] = '\0';
	return std::string(buf);
}

static bool SameLuid(const LUID &a, const LUID &b)
{
	return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

static void CloseSessionInternal()
{
	auto unreg = reinterpret_cast<PFN_Unregister>(g_ofa.slot[5]);
	if (unreg) {
		for (int i = 0; i < 2; ++i) {
			if (g_ofa.reg_src[i])
				SafeUnregister(unreg, g_ofa.reg_src[i]);
		}
		if (g_ofa.reg_flow)
			SafeUnregister(unreg, g_ofa.reg_flow);
	}
	g_ofa.reg_src[0] = g_ofa.reg_src[1] = g_ofa.reg_flow = nullptr;

	if (g_ofa.session) {
		auto destroy = reinterpret_cast<PFN_Destroy>(g_ofa.slot[7]);
		if (destroy)
			SafeDestroy(destroy, g_ofa.session);
		g_ofa.session = nullptr;
	}

	g_ofa.src[0].Reset();
	g_ofa.src[1].Reset();
	g_ofa.flow.Reset();
	g_ofa.flow_staging.Reset();
	g_ofa.context.Reset();
	g_ofa.device.Reset();
	g_ofa.width = g_ofa.height = 0;
	g_ofa.flow_width = g_ofa.flow_height = 0;
	g_ofa.current = 0;
	g_ofa.primed = false;
	g_ofa.have_luid = false;
	g_ofa.luma_upload.clear();
}

static bool EnsureFunctionTable(std::string &error)
{
	if (g_ofa.lib && g_ofa.slot[0] && g_ofa.slot[1] && g_ofa.slot[4] && g_ofa.slot[5] && g_ofa.slot[6] &&
	    g_ofa.slot[7])
		return true;

	if (!g_ofa.lib) {
		g_ofa.lib = LoadLibraryW(L"nvofapi64.dll");
		if (!g_ofa.lib) {
			error = "NVIDIA Optical Flow: could not load nvofapi64.dll from the NVIDIA display driver.";
			g_ofa.diagnostics = error;
			return false;
		}
	}

	auto create = reinterpret_cast<PFN_CreateInstance>(GetProcAddress(g_ofa.lib, "NvOFAPICreateInstanceD3D11"));
	if (!create) {
		error = "NVIDIA Optical Flow: nvofapi64.dll does not export NvOFAPICreateInstanceD3D11.";
		g_ofa.diagnostics = error;
		return false;
	}

	memset(g_ofa.slot, 0, sizeof(g_ofa.slot));
	const NvOfStatus r = SafeCreateInstance(create, kOfApiVersion, g_ofa.slot);
	if (r != kOfSuccess) {
		char msg[320];
		std::snprintf(msg, sizeof(msg),
			      "NVIDIA Optical Flow: API 0x20 function-table creation failed: 0x%08X (%s).",
			      StatusBits(r), r == kOfRaised ? "driver call raised an exception" : StatusName(r));
		error = msg;
		g_ofa.last_status = StatusBits(r);
		RefreshDiagnostics("CreateInstanceD3D11");
		return false;
	}
	if (!g_ofa.slot[0] || !g_ofa.slot[1] || !g_ofa.slot[4] || !g_ofa.slot[5] || !g_ofa.slot[6] || !g_ofa.slot[7]) {
		error = "NVIDIA Optical Flow: D3D11 function table is incomplete on this driver.";
		g_ofa.diagnostics = error;
		return false;
	}
	RefreshDiagnostics("function table ready");
	return true;
}

static ComPtr<ID3D11Texture2D> MakeTexture(ID3D11Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format,
					   D3D11_USAGE usage, UINT bind_flags, UINT cpu_access)
{
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = usage;
	desc.BindFlags = bind_flags;
	desc.CPUAccessFlags = cpu_access;
	ComPtr<ID3D11Texture2D> tex;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &tex)))
		return nullptr;
	return tex;
}

static ComPtr<ID3D11Texture2D> MakeInputTexture(ID3D11Device *device, uint32_t width, uint32_t height,
						DXGI_FORMAT format)
{
	// NVOF accesses these textures through registered resource handles; it does
	// not need an SRV/UAV from this bridge. Prefer an unbound DEFAULT texture,
	// then fall back to the bindings used by earlier project releases.
	const UINT attempts[] = {
		0u,
		D3D11_BIND_SHADER_RESOURCE,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
	};
	for (UINT flags : attempts) {
		if (auto t = MakeTexture(device, width, height, format, D3D11_USAGE_DEFAULT, flags, 0))
			return t;
	}
	return nullptr;
}

static ComPtr<ID3D11Texture2D> MakeOutputTexture(ID3D11Device *device, uint32_t width, uint32_t height,
						 DXGI_FORMAT format)
{
	const UINT attempts[] = {
		0u,
		D3D11_BIND_UNORDERED_ACCESS,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
	};
	for (UINT flags : attempts) {
		if (auto t = MakeTexture(device, width, height, format, D3D11_USAGE_DEFAULT, flags, 0))
			return t;
	}
	return nullptr;
}

static bool QueryFormats(uint32_t usage, std::vector<DXGI_FORMAT> &formats, std::string &warning)
{
	formats.clear();
	if (!g_ofa.session || !g_ofa.slot[2] || !g_ofa.slot[3]) {
		warning = "surface-format query functions unavailable";
		return false;
	}
	auto count_fn = reinterpret_cast<PFN_GetSurfaceFormatCount>(g_ofa.slot[2]);
	auto format_fn = reinterpret_cast<PFN_GetSurfaceFormat>(g_ofa.slot[3]);
	uint32_t count = 0;
	NvOfStatus r = SafeGetSurfaceFormatCount(count_fn, g_ofa.session, usage, kModeOpticalFlow, &count);
	if (r != kOfSuccess || count == 0 || count > 128) {
		std::ostringstream os;
		os << "surface-format count query failed: 0x" << std::hex << StatusBits(r) << std::dec;
		warning = os.str();
		return false;
	}
	formats.resize(count, DXGI_FORMAT_UNKNOWN);
	r = SafeGetSurfaceFormat(format_fn, g_ofa.session, usage, kModeOpticalFlow, formats.data());
	if (r != kOfSuccess) {
		formats.clear();
		std::ostringstream os;
		os << "surface-format query failed: 0x" << std::hex << StatusBits(r) << std::dec;
		warning = os.str();
		return false;
	}
	return true;
}

static bool QueryOutputGrids(std::vector<uint32_t> &grids, std::string &warning)
{
	grids.clear();
	if (!g_ofa.session || !g_ofa.slot[9]) {
		warning = "NvOFGetCaps unavailable";
		return false;
	}
	auto caps_fn = reinterpret_cast<PFN_GetCaps>(g_ofa.slot[9]);
	uint32_t count = 0;
	NvOfStatus r = SafeGetCaps(caps_fn, g_ofa.session, kCapsOutputGridSizes, nullptr, &count);
	if (r != kOfSuccess || count == 0 || count > 32) {
		std::ostringstream os;
		os << "output-grid capability count query failed: 0x" << std::hex << StatusBits(r) << std::dec;
		warning = os.str();
		return false;
	}
	grids.resize(count, 0);
	r = SafeGetCaps(caps_fn, g_ofa.session, kCapsOutputGridSizes, grids.data(), &count);
	if (r != kOfSuccess) {
		grids.clear();
		std::ostringstream os;
		os << "output-grid capability query failed: 0x" << std::hex << StatusBits(r) << std::dec;
		warning = os.str();
		return false;
	}
	if (count < grids.size())
		grids.resize(count);
	return !grids.empty();
}

static bool ContainsFormat(const std::vector<DXGI_FORMAT> &formats, DXGI_FORMAT wanted)
{
	return std::find(formats.begin(), formats.end(), wanted) != formats.end();
}

static bool ContainsGrid(const std::vector<uint32_t> &grids, uint32_t wanted)
{
	return std::find(grids.begin(), grids.end(), wanted) != grids.end();
}

static DXGI_FORMAT ChooseInputFormat(const std::vector<DXGI_FORMAT> &formats)
{
	// We feed luminance to NVOF, so prefer the driver's native single-channel
	// 8-bit surface. Fall back to the RGB formats used by earlier releases.
	const DXGI_FORMAT preferred[] = {
		DXGI_FORMAT_R8_UNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM,
	};
	for (DXGI_FORMAT f : preferred)
		if (ContainsFormat(formats, f))
			return f;
	return DXGI_FORMAT_UNKNOWN;
}

static uint32_t ChooseGrid(const std::vector<uint32_t> &grids)
{
	if (ContainsGrid(grids, 2))
		return 2;
	if (ContainsGrid(grids, 1))
		return 1;
	if (ContainsGrid(grids, 4))
		return 4;
	return kPreferredGrid;
}

static uint32_t InputBytesPerPixel(DXGI_FORMAT format)
{
	if (format == DXGI_FORMAT_R8_UNORM)
		return 1;
	if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM)
		return 4;
	return 0;
}

static bool OpenSession(IDXGIAdapter1 *adapter, uint32_t width, uint32_t height, std::string &error)
{
	if (!adapter) {
		error = "NVIDIA Optical Flow: selected DXGI adapter is null.";
		return false;
	}
	if (!EnsureFunctionTable(error))
		return false;

	DXGI_ADAPTER_DESC1 ad{};
	if (FAILED(adapter->GetDesc1(&ad))) {
		error = "NVIDIA Optical Flow: failed to query the selected DXGI adapter.";
		return false;
	}

	if (g_ofa.session && g_ofa.width == width && g_ofa.height == height && g_ofa.have_luid &&
	    SameLuid(g_ofa.adapter_luid, ad.AdapterLuid))
		return true;

	CloseSessionInternal();
	g_ofa.supported_grids.clear();
	g_ofa.supported_input_formats.clear();
	g_ofa.supported_output_formats.clear();
	g_ofa.input_format = DXGI_FORMAT_UNKNOWN;
	g_ofa.output_format = DXGI_FORMAT_UNKNOWN;
	g_ofa.selected_grid = kPreferredGrid;
	g_ofa.last_status = 0;
	g_ofa.last_driver_error.clear();

	D3D_FEATURE_LEVEL feature_level{};
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
				       g_ofa.device.GetAddressOf(), &feature_level, g_ofa.context.GetAddressOf());
	if (FAILED(hr) || !g_ofa.device || !g_ofa.context) {
		char msg[256];
		std::snprintf(msg, sizeof(msg),
			      "NVIDIA Optical Flow: D3D11CreateDevice on the selected NVIDIA adapter failed: 0x%08X.",
			      static_cast<unsigned>(hr));
		error = msg;
		RefreshDiagnostics("D3D11CreateDevice failed");
		return false;
	}

	g_ofa.width = width;
	g_ofa.height = height;
	g_ofa.adapter_luid = ad.AdapterLuid;
	g_ofa.have_luid = true;

	auto create_session = reinterpret_cast<PFN_CreateD3D11>(g_ofa.slot[0]);
	NvOfStatus r = SafeCreateSession(create_session, g_ofa.device.Get(), g_ofa.context.Get(), &g_ofa.session);
	if (r != kOfSuccess || !g_ofa.session) {
		char msg[320];
		std::snprintf(msg, sizeof(msg),
			      "NVIDIA Optical Flow: could not open a session on the selected GPU: 0x%08X (%s).",
			      StatusBits(r), r == kOfRaised ? "driver call raised an exception" : StatusName(r));
		error = msg;
		g_ofa.last_status = StatusBits(r);
		RefreshDiagnostics("CreateOpticalFlowD3D11");
		CloseSessionInternal();
		return false;
	}

	// NVIDIA documents capability probing before NvOFInit. Surface-format
	// probing belongs to buffer allocation and is done after NvOFInit below.
	// If an older driver lacks the optional capability slot, preserve the
	// grid-2 assumption rather than regressing known-good configurations.
	std::string caps_warning, input_warning, output_warning;
	const bool have_caps = QueryOutputGrids(g_ofa.supported_grids, caps_warning);
	if (have_caps)
		g_ofa.selected_grid = ChooseGrid(g_ofa.supported_grids);
	if (have_caps && !ContainsGrid(g_ofa.supported_grids, g_ofa.selected_grid)) {
		error = "NVIDIA Optical Flow: no supported output grid (1/2/4) was reported by the driver.";
		RefreshDiagnostics("grid selection failed");
		CloseSessionInternal();
		return false;
	}

	g_ofa.flow_width = (width + g_ofa.selected_grid - 1) / g_ofa.selected_grid;
	g_ofa.flow_height = (height + g_ofa.selected_grid - 1) / g_ofa.selected_grid;

	NvOfInitParams init{};
	init.width = width;
	init.height = height;
	init.outGridSize = g_ofa.selected_grid;
	init.hintGridSize = g_ofa.selected_grid;
	init.mode = kModeOpticalFlow;
	init.perfLevel = kPerf;
	init.enableExternalHints = 0;
	init.enableOutputCost = 0;
	init.hPrivData = nullptr;
	init.disparityRange = 0;
	init.enableRoi = 0;

	auto init_fn = reinterpret_cast<PFN_Init>(g_ofa.slot[1]);
	r = SafeInit(init_fn, g_ofa.session, &init);
	if (r != kOfSuccess) {
		g_ofa.last_driver_error = LastError();
		char msg[768];
		std::snprintf(msg, sizeof(msg),
			      "NVIDIA Optical Flow: nvOFInit refused %ux%u grid %u / FAST: 0x%08X (%s). %s", width,
			      height, g_ofa.selected_grid, StatusBits(r),
			      r == kOfRaised ? "driver call raised an exception" : StatusName(r),
			      g_ofa.last_driver_error.c_str());
		error = msg;
		g_ofa.last_status = StatusBits(r);
		RefreshDiagnostics("NvOFInit failed");
		CloseSessionInternal();
		return false;
	}

	// Buffer/surface-format queries are documented as part of D3D11 buffer
	// allocation, after NvOFInit. Query both usages and allocate only formats
	// advertised by this driver/GPU. Legacy fallback keeps earlier behavior.
	const bool have_input_formats = QueryFormats(kUsageInput, g_ofa.supported_input_formats, input_warning);
	const bool have_output_formats = QueryFormats(kUsageOutput, g_ofa.supported_output_formats, output_warning);
	if (have_input_formats)
		g_ofa.input_format = ChooseInputFormat(g_ofa.supported_input_formats);
	else
		g_ofa.input_format = DXGI_FORMAT_B8G8R8A8_UNORM;
	if (have_output_formats) {
		if (ContainsFormat(g_ofa.supported_output_formats, DXGI_FORMAT_R16G16_SINT))
			g_ofa.output_format = DXGI_FORMAT_R16G16_SINT;
	} else {
		g_ofa.output_format = DXGI_FORMAT_R16G16_SINT;
	}

	if (g_ofa.input_format == DXGI_FORMAT_UNKNOWN) {
		std::ostringstream os;
		os << "NVIDIA Optical Flow: driver does not advertise a supported simple 8-bit D3D11 input format. Advertised: "
		   << FormatList(g_ofa.supported_input_formats) << ".";
		error = os.str();
		RefreshDiagnostics("input format selection failed");
		CloseSessionInternal();
		return false;
	}
	if (g_ofa.output_format == DXGI_FORMAT_UNKNOWN) {
		std::ostringstream os;
		os << "NVIDIA Optical Flow: driver does not advertise R16G16_SINT for optical-flow output. Advertised: "
		   << FormatList(g_ofa.supported_output_formats) << ".";
		error = os.str();
		RefreshDiagnostics("output format selection failed");
		CloseSessionInternal();
		return false;
	}

	g_ofa.src[0] = MakeInputTexture(g_ofa.device.Get(), width, height, g_ofa.input_format);
	g_ofa.src[1] = MakeInputTexture(g_ofa.device.Get(), width, height, g_ofa.input_format);
	g_ofa.flow = MakeOutputTexture(g_ofa.device.Get(), g_ofa.flow_width, g_ofa.flow_height, g_ofa.output_format);
	g_ofa.flow_staging = MakeTexture(g_ofa.device.Get(), g_ofa.flow_width, g_ofa.flow_height, g_ofa.output_format,
					 D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
	if (!g_ofa.src[0] || !g_ofa.src[1] || !g_ofa.flow || !g_ofa.flow_staging) {
		std::ostringstream os;
		os << "NVIDIA Optical Flow: failed to create D3D11 textures using input="
		   << FormatName(g_ofa.input_format) << ", output=" << FormatName(g_ofa.output_format) << ".";
		error = os.str();
		RefreshDiagnostics("D3D11 texture allocation failed");
		CloseSessionInternal();
		return false;
	}

	auto reg = reinterpret_cast<PFN_Register>(g_ofa.slot[4]);
	NvOfStatus r0 = SafeRegister(reg, g_ofa.session, g_ofa.src[0].Get(), &g_ofa.reg_src[0]);
	NvOfStatus r1 = SafeRegister(reg, g_ofa.session, g_ofa.src[1].Get(), &g_ofa.reg_src[1]);
	NvOfStatus r2 = SafeRegister(reg, g_ofa.session, g_ofa.flow.Get(), &g_ofa.reg_flow);
	if (r0 != kOfSuccess || r1 != kOfSuccess || r2 != kOfSuccess || !g_ofa.reg_src[0] || !g_ofa.reg_src[1] ||
	    !g_ofa.reg_flow) {
		g_ofa.last_driver_error = LastError();
		char msg[768];
		std::snprintf(
			msg, sizeof(msg),
			"NVIDIA Optical Flow: resource registration failed (0x%08X, 0x%08X, 0x%08X), input=%s, output=%s. %s",
			StatusBits(r0), StatusBits(r1), StatusBits(r2), FormatName(g_ofa.input_format).c_str(),
			FormatName(g_ofa.output_format).c_str(), g_ofa.last_driver_error.c_str());
		error = msg;
		g_ofa.last_status = StatusBits(r0 != kOfSuccess ? r0 : (r1 != kOfSuccess ? r1 : r2));
		RefreshDiagnostics("resource registration failed");
		CloseSessionInternal();
		return false;
	}

	g_ofa.current = 0;
	g_ofa.primed = false;
	const uint32_t bpp = InputBytesPerPixel(g_ofa.input_format);
	g_ofa.luma_upload.resize(static_cast<size_t>(width) * height * bpp);

	std::ostringstream phase;
	phase << "ready";
	if (!caps_warning.empty())
		phase << "; caps fallback: " << caps_warning;
	if (!input_warning.empty())
		phase << "; input-format fallback: " << input_warning;
	if (!output_warning.empty())
		phase << "; output-format fallback: " << output_warning;
	g_ofa.last_status = 0;
	g_ofa.last_driver_error.clear();
	RefreshDiagnostics(phase.str().c_str());
	return true;
}

static bool UploadLuma(const uint8_t *bgra, int row_pitch, std::string &error)
{
	const size_t pixels = static_cast<size_t>(g_ofa.width) * g_ofa.height;
	const uint32_t bpp = InputBytesPerPixel(g_ofa.input_format);
	if (!bpp) {
		error = "NVIDIA Optical Flow: selected input format has no CPU upload conversion.";
		return false;
	}
	if (g_ofa.luma_upload.size() != pixels * bpp)
		g_ofa.luma_upload.resize(pixels * bpp);

	// Rec.709 luma from BGRA8 rows; integer approximation of
	// 0.2126 R + 0.7152 G + 0.0722 B scaled to 0..255.
	for (uint32_t y = 0; y < g_ofa.height; ++y) {
		const uint8_t *src = bgra + static_cast<size_t>(y) * row_pitch;
		uint8_t *dst_row = g_ofa.luma_upload.data() + static_cast<size_t>(y) * g_ofa.width * bpp;
		for (uint32_t x = 0; x < g_ofa.width; ++x) {
			const uint32_t b = src[x * 4 + 0], g = src[x * 4 + 1], r = src[x * 4 + 2];
			const uint8_t v = static_cast<uint8_t>((13938u * r + 46871u * g + 4729u * b + 0x8000u) >> 16);
			if (bpp == 1) {
				dst_row[x] = v;
			} else {
				dst_row[x * 4 + 0] = v;
				dst_row[x * 4 + 1] = v;
				dst_row[x * 4 + 2] = v;
				dst_row[x * 4 + 3] = 255;
			}
		}
	}

	g_ofa.context->UpdateSubresource(g_ofa.src[g_ofa.current].Get(), 0, nullptr, g_ofa.luma_upload.data(),
					 g_ofa.width * bpp, 0);
	return true;
}

} // namespace

bool NvofPrepareFrame(IDXGIAdapter1 *adapter, const uint8_t *bgra, int row_pitch, uint32_t width, uint32_t height,
		      bool reset, NvofFlowFrame &out, std::string &error)
{
	out = NvofFlowFrame{};
	error.clear();
	if (!bgra || row_pitch <= 0 || width == 0 || height == 0) {
		error = "NVIDIA Optical Flow: invalid BGRA frame or dimensions.";
		return false;
	}
	if (!OpenSession(adapter, width, height, error))
		return false;

	if (reset) {
		g_ofa.current = 0;
		g_ofa.primed = false;
	}

	if (!UploadLuma(bgra, row_pitch, error)) {
		RefreshDiagnostics("input upload failed");
		return false;
	}

	if (!g_ofa.primed) {
		// There is no previous frame. Prime one input slot and let DLSS receive
		// the explicit zero-MV texture for the first output frame.
		g_ofa.primed = true;
		g_ofa.current ^= 1;
		out.has_flow = false;
		out.grid = g_ofa.selected_grid;
		RefreshDiagnostics("first frame primed; zero MV supplied to DLSS");
		return true;
	}

	NvOfExecuteInputParams in{};
	NvOfExecuteOutputParams exec_out{};
	in.inputFrame = g_ofa.reg_src[g_ofa.current];         // current
	in.referenceFrame = g_ofa.reg_src[g_ofa.current ^ 1]; // previous
	in.externalHints = nullptr;
	// Pairwise mode: NVOF's own temporal hints stay disabled so each frame pair
	// is deterministic and a new stream can reset cleanly.
	in.disableTemporalHints = 1;
	exec_out.outputBuffer = g_ofa.reg_flow;

	auto exec = reinterpret_cast<PFN_Execute>(g_ofa.slot[6]);
	const NvOfStatus r = SafeExecute(exec, g_ofa.session, &in, &exec_out);
	if (r != kOfSuccess) {
		g_ofa.last_driver_error = LastError();
		g_ofa.last_status = StatusBits(r);
		RefreshDiagnostics("NvOFExecute failed");
		char msg[1024];
		std::snprintf(
			msg, sizeof(msg),
			"NVIDIA Optical Flow: nvOFExecute failed: 0x%08X (%s), input=%s, output=%s, grid=%u, %ux%u. %s",
			StatusBits(r), r == kOfRaised ? "driver call raised an exception" : StatusName(r),
			FormatName(g_ofa.input_format).c_str(), FormatName(g_ofa.output_format).c_str(),
			g_ofa.selected_grid, g_ofa.width, g_ofa.height, g_ofa.last_driver_error.c_str());
		error = msg;
		return false;
	}

	// Copy to staging + Map is the synchronization point for the private D3D11
	// device. D3D11 NVOF handles device synchronization internally.
	g_ofa.context->CopyResource(g_ofa.flow_staging.Get(), g_ofa.flow.Get());
	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = g_ofa.context->Map(g_ofa.flow_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr) || !mapped.pData) {
		char msg[256];
		std::snprintf(msg, sizeof(msg), "NVIDIA Optical Flow: flow staging Map failed: 0x%08X.",
			      static_cast<unsigned>(hr));
		error = msg;
		RefreshDiagnostics("flow staging Map failed");
		return false;
	}

	out.has_flow = true;
	out.width = g_ofa.flow_width;
	out.height = g_ofa.flow_height;
	out.grid = g_ofa.selected_grid;
	out.xy.resize(static_cast<size_t>(out.width) * out.height * 2);
	for (uint32_t y = 0; y < out.height; ++y) {
		const int16_t *src = reinterpret_cast<const int16_t *>(static_cast<const uint8_t *>(mapped.pData) +
								       static_cast<size_t>(y) * mapped.RowPitch);
		int16_t *dst = out.xy.data() + static_cast<size_t>(y) * out.width * 2;
		memcpy(dst, src, static_cast<size_t>(out.width) * 2 * sizeof(int16_t));
	}
	g_ofa.context->Unmap(g_ofa.flow_staging.Get(), 0);

	g_ofa.current ^= 1;
	g_ofa.last_status = 0;
	g_ofa.last_driver_error.clear();
	RefreshDiagnostics("NvOFExecute succeeded");
	return true;
}

void NvofReleaseSession()
{
	CloseSessionInternal();
}

void NvofShutdown()
{
	CloseSessionInternal();
	memset(g_ofa.slot, 0, sizeof(g_ofa.slot));
	if (g_ofa.lib) {
		FreeLibrary(g_ofa.lib);
		g_ofa.lib = nullptr;
	}
}

uint32_t NvofGridSize()
{
	return g_ofa.selected_grid ? g_ofa.selected_grid : kPreferredGrid;
}

const char *NvofDiagnostics()
{
	return g_ofa.diagnostics.c_str();
}
