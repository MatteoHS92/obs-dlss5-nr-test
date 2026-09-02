// obs-dlss5-nr — DLSS 5 Neural Rendering filter for OBS Studio
// Copyright (C) 2026 Saganaki22
//
// Bridge interface between the filter and NVIDIA DLSS NR (NGX feature 18) on
// D3D12. Frames cross as CPU-staged BGRA buffers (zero-copy D3D11<->D3D12
// interop is planned as a later optimization).

#pragma once

#include <cstdint>

struct NrBridgeParams {
	int style;
	int preset;
	float intensity;
	float tone;
	float structure;
	float skin;
	int automask;
	int ui_correction;
	int reset;
};

namespace nrbridge {

// gpu_index: index among NVIDIA (VendorId 0x10DE) adapters.
// runtime_dir: directory containing the user-supplied nvngx_dlssnr.dll (UTF-16).
// shim_dir: directory containing nvngx.dll_obs.dll (UTF-16); may be null.
bool init(int gpu_index, const wchar_t *runtime_dir, const wchar_t *shim_dir);

// src_bgra/dst_bgra: 8-bit BGRA rows; pitches in bytes.
bool process(const uint8_t *src_bgra, int src_row_pitch, uint8_t *dst_bgra, int dst_row_pitch, int width, int height,
	     const NrBridgeParams &params);

void shutdown();
bool ready();
const char *gpu_name();
const char *last_error();

} // namespace nrbridge
