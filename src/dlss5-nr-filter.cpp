// obs-dlss5-nr — DLSS 5 Neural Rendering filter for OBS Studio
// Copyright (C) 2026 Saganaki22
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Runs video frames through NVIDIA DLSS NR (NGX feature 18) on a private
// D3D12 device. Frames are CPU-staged: OBS texture -> stage surface ->
// bridge (D3D12 upload -> NR -> readback) -> OBS texture. Any bridge failure
// falls back to clean pass-through video — the stream never stops.

#include "dlss5-nr-filter.h"

#include <obs-module.h>
#include <util/base.h>
#include <util/platform.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "bridge/nr_bridge.h"

// DLSSNR.Style values, as consumed by nvngx_dlssnr (NGX feature 18).
enum DlssNrStyle {
	DLSSNR_STYLE_DEFAULT = 0,
	DLSSNR_STYLE_NATURAL = 1,
	DLSSNR_STYLE_CINEMATIC = 2,
};

enum DlssNrChannelOrder {
	DLSSNR_CHANNEL_AUTO = 0,
	DLSSNR_CHANNEL_RGBA = 1,
	DLSSNR_CHANNEL_BGRA = 2,
};

static constexpr uint64_t LOG_THROTTLE_NS = 5000000000ULL; // 5 s
static constexpr uint64_t INIT_RETRY_NS = 5000000000ULL;   // 5 s

struct dlss5nr_filter {
	obs_source_t *context;
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurface;
	gs_texture_t *out_tex;
	uint32_t surface_w = 0, surface_h = 0;
	std::vector<uint8_t> out_buf;

	// Mirrored settings (consumed by the bridge).
	int style = DLSSNR_STYLE_NATURAL;
	int preset = 3;
	float intensity = 1.0f;
	float tone = 1.0f;
	float structure = 1.0f;
	float skin = -1.0f;
	bool automask = false;
	int ui_correction = 0;
	int gpu_index = 0;
	int channel_order = DLSSNR_CHANNEL_AUTO;
	bool reset_pending = false;
	bool initialized = false;

	// Bridge lifecycle / status.
	bool init_attempted = false;
	uint64_t last_init_attempt = 0;
	uint64_t last_error_log = 0;
	std::mutex status_mutex;
	std::string status_text;

	void set_status(const char *fmt, ...)
	{
		char buf[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		std::lock_guard<std::mutex> lock(status_mutex);
		status_text = buf;
	}
};

static std::atomic<int> g_bridge_users{0};

// ---------------------------------------------------------------- lifecycle

static const char *dlss5nr_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("FilterName");
}

static void *dlss5nr_create(obs_data_t *settings, obs_source_t *context)
{
	auto *f = new dlss5nr_filter{};
	f->context = context;

	obs_enter_graphics();
	f->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	obs_leave_graphics();

	if (!f->texrender) {
		blog(LOG_ERROR, "[obs-dlss5-nr] failed to create texrender");
		delete f;
		return nullptr;
	}

	f->set_status("%s", obs_module_text("Status.Initializing"));
	g_bridge_users.fetch_add(1, std::memory_order_relaxed);

	obs_source_update(context, settings);
	return f;
}

static void dlss5nr_destroy(void *data)
{
	auto *f = static_cast<dlss5nr_filter *>(data);
	if (!f)
		return;

	obs_enter_graphics();
	if (f->texrender)
		gs_texrender_destroy(f->texrender);
	if (f->stagesurface)
		gs_stagesurface_destroy(f->stagesurface);
	if (f->out_tex)
		gs_texture_destroy(f->out_tex);
	obs_leave_graphics();

	if (g_bridge_users.fetch_sub(1, std::memory_order_relaxed) == 1)
		nrbridge::shutdown();

	delete f;
}

static uint32_t dlss5nr_width(void *data)
{
	auto *f = static_cast<dlss5nr_filter *>(data);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	return parent ? obs_source_get_base_width(parent) : 0;
}

static uint32_t dlss5nr_height(void *data)
{
	auto *f = static_cast<dlss5nr_filter *>(data);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	return parent ? obs_source_get_base_height(parent) : 0;
}

// ------------------------------------------------------------- bridge setup

static wchar_t *utf8_to_wide(const char *s)
{
	wchar_t *out = nullptr;
	if (s && os_utf8_to_wcs_ptr(s, 0, &out) > 0)
		return out;
	return out;
}

static std::string dir_of(const std::string &path)
{
	size_t p = path.find_last_of("/\\");
	return p == std::string::npos ? std::string() : path.substr(0, p);
}

static bool find_runtime_dll(std::string *dll_path_out, std::string *expected_path_out)
{
	char *cfg = obs_module_config_path("runtime\\nvngx_dlssnr.dll");
	std::string cfg_path = cfg ? cfg : "";
	bfree(cfg);

	if (os_file_exists(cfg_path.c_str())) {
		*dll_path_out = cfg_path;
		*expected_path_out = cfg_path;
		return true;
	}

	char *data = obs_module_file("runtime\\nvngx_dlssnr.dll");
	std::string data_path = data ? data : "";
	bfree(data);

	*expected_path_out = cfg_path;
	if (!data_path.empty() && os_file_exists(data_path.c_str())) {
		*dll_path_out = data_path;
		return true;
	}
	return false;
}

static bool ensure_bridge_ready(dlss5nr_filter *f)
{
	if (nrbridge::ready())
		return true;

	const uint64_t now = os_gettime_ns();
	if (f->init_attempted && now - f->last_init_attempt < INIT_RETRY_NS)
		return false;

	f->init_attempted = true;
	f->last_init_attempt = now;

	std::string dll_path, expected;
	if (!find_runtime_dll(&dll_path, &expected)) {
		f->set_status("nvngx_dlssnr.dll not found — place it at:\n%s", expected.c_str());
		return false;
	}

	// Shim is installed next to the plugin binary.
	const char *bin = obs_get_module_binary_path(obs_current_module());
	std::string shim_dir = bin ? dir_of(bin) : std::string();

	const int gpu_index = f->gpu_index;
	wchar_t *runtime_dir_w = utf8_to_wide(dir_of(dll_path).c_str());
	wchar_t *shim_dir_w = utf8_to_wide(shim_dir.c_str());
	const bool ok = nrbridge::init(gpu_index, runtime_dir_w, shim_dir_w);
	bfree(runtime_dir_w);
	bfree(shim_dir_w);

	if (!ok) {
		f->set_status("DLSS NR init failed: %s", nrbridge::last_error());
		return false;
	}

	f->set_status("Ready — %s\n%s", nrbridge::gpu_name(), dll_path.c_str());
	blog(LOG_INFO, "[obs-dlss5-nr] bridge initialized: %s (runtime: %s)", nrbridge::gpu_name(), dll_path.c_str());
	return true;
}

static void log_error_throttled(dlss5nr_filter *f, const char *err)
{
	const uint64_t now = os_gettime_ns();
	if (now - f->last_error_log < LOG_THROTTLE_NS)
		return;
	f->last_error_log = now;
	blog(LOG_WARNING, "[obs-dlss5-nr] NR processing failed, passing through: %s", err);
}

// ---------------------------------------------------------------- rendering

static bool ensure_surfaces(dlss5nr_filter *f, uint32_t cx, uint32_t cy)
{
	if (f->stagesurface && f->out_tex && f->surface_w == cx && f->surface_h == cy)
		return true;

	obs_enter_graphics();
	if (f->stagesurface) {
		gs_stagesurface_destroy(f->stagesurface);
		f->stagesurface = nullptr;
	}
	if (f->out_tex) {
		gs_texture_destroy(f->out_tex);
		f->out_tex = nullptr;
	}
	f->stagesurface = gs_stagesurface_create(cx, cy, GS_BGRA);
	f->out_tex = gs_texture_create(cx, cy, GS_BGRA, 1, nullptr, GS_DYNAMIC);
	obs_leave_graphics();

	f->surface_w = cx;
	f->surface_h = cy;
	f->out_buf.resize((size_t)cx * cy * 4);

	if (!f->stagesurface || !f->out_tex) {
		f->set_status("Failed to allocate OBS staging surfaces");
		return false;
	}
	return true;
}

static void dlss5nr_video_render(void *data, gs_effect_t *filter_effect)
{
	UNUSED_PARAMETER(filter_effect);
	auto *f = static_cast<dlss5nr_filter *>(data);
	obs_source_t *context = f->context;
	obs_source_t *parent = obs_filter_get_parent(context);

	const uint32_t cx = parent ? obs_source_get_base_width(parent) : 0;
	const uint32_t cy = parent ? obs_source_get_base_height(parent) : 0;
	if (!parent || !f->texrender || cx == 0 || cy == 0) {
		obs_source_skip_video_filter(context);
		return;
	}

	const bool bridge_ready = ensure_bridge_ready(f);
	if (bridge_ready && ensure_surfaces(f, cx, cy)) {
		// Grab the parent's rendered frame.
		gs_texrender_reset(f->texrender);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		bool grabbed = false;
		if (gs_texrender_begin(f->texrender, cx, cy)) {
			gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
			obs_source_video_render(parent);
			gs_texrender_end(f->texrender);
			grabbed = true;
		}
		gs_blend_state_pop();

		if (grabbed) {
			gs_texture_t *src_tex = gs_texrender_get_texture(f->texrender);
			if (src_tex) {
				gs_stage_texture(f->stagesurface, src_tex);
				uint8_t *mapped = nullptr;
				uint32_t pitch = 0;
				if (gs_stagesurface_map(f->stagesurface, &mapped, &pitch)) {
					NrBridgeParams p{};
					p.style = f->style;
					p.preset = f->preset;
					p.intensity = f->intensity;
					p.tone = f->tone;
					p.structure = f->structure;
					p.skin = f->skin;
					p.automask = f->automask ? 1 : 0;
					p.ui_correction = f->ui_correction;
					p.reset = f->reset_pending ? 1 : 0;
					f->reset_pending = false;

					const bool ok = nrbridge::process(mapped, (int)pitch, f->out_buf.data(),
									  (int)cx * 4, (int)cx, (int)cy, p);
					gs_stagesurface_unmap(f->stagesurface);

					if (ok) {
						gs_texture_set_image(f->out_tex, f->out_buf.data(), cx * 4, false);

						gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
						gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
						gs_effect_set_texture(image, f->out_tex);
						while (gs_effect_loop(effect, "Draw"))
							gs_draw_sprite(f->out_tex, 0, cx, cy);
						return;
					}

					f->set_status("NR failed — passing through. %s", nrbridge::last_error());
					log_error_throttled(f, nrbridge::last_error());
				}
			}
		}
	} else if (!bridge_ready && f->reset_pending) {
		f->reset_pending = false;
	}

	obs_source_skip_video_filter(context);
}

// ----------------------------------------------------------------- settings

static void dlss5nr_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<dlss5nr_filter *>(data);

	const long long new_style = obs_data_get_int(settings, "style");
	if (f->initialized && new_style != f->style)
		blog(LOG_INFO, "[obs-dlss5-nr] style changed to %lld — NR feature will be rebuilt", new_style);
	f->style = (int)new_style;
	f->preset = (int)obs_data_get_int(settings, "preset");
	f->intensity = (float)obs_data_get_double(settings, "intensity");
	f->tone = (float)obs_data_get_double(settings, "tone");
	f->structure = (float)obs_data_get_double(settings, "structure");
	f->skin = (float)obs_data_get_double(settings, "skin");
	f->automask = obs_data_get_bool(settings, "automask");
	f->ui_correction = (int)obs_data_get_int(settings, "ui_correction");
	f->gpu_index = (int)obs_data_get_int(settings, "gpu_index");
	f->channel_order = (int)obs_data_get_int(settings, "channel_order");
	f->initialized = true;
}

static bool dlss5nr_reset_history_cb(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	auto *f = static_cast<dlss5nr_filter *>(data);
	f->reset_pending = true;
	blog(LOG_INFO, "[obs-dlss5-nr] history reset requested");
	return false;
}

static obs_properties_t *dlss5nr_properties(void *data)
{
	auto *f = static_cast<dlss5nr_filter *>(data);

	// Push the current status into the settings so the info text shows it.
	{
		std::lock_guard<std::mutex> lock(f->status_mutex);
		obs_data_t *s = obs_source_get_settings(f->context);
		obs_data_set_string(s, "status", f->status_text.c_str());
		obs_data_release(s);
	}

	obs_properties_t *props = obs_properties_create();

	obs_property_t *status = obs_properties_add_text(props, "status", obs_module_text("Status"), OBS_TEXT_INFO);
	obs_property_text_set_info_type(status, OBS_TEXT_INFO_NORMAL);

	obs_property_t *style = obs_properties_add_list(props, "style", obs_module_text("Style"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, obs_module_text("Style.Default"), DLSSNR_STYLE_DEFAULT);
	obs_property_list_add_int(style, obs_module_text("Style.Natural"), DLSSNR_STYLE_NATURAL);
	obs_property_list_add_int(style, obs_module_text("Style.Cinematic"), DLSSNR_STYLE_CINEMATIC);

	obs_property_t *preset = obs_properties_add_list(props, "preset", obs_module_text("Preset"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(preset, "0", 0);
	obs_property_list_add_int(preset, "1", 1);
	obs_property_list_add_int(preset, "2", 2);
	obs_property_list_add_int(preset, "3", 3);

	obs_properties_add_float_slider(props, "intensity", obs_module_text("Intensity"), 0.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, "tone", obs_module_text("LocalTone"), 0.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, "structure", obs_module_text("LocalStructure"), 0.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, "skin", obs_module_text("Skin"), -1.0, 2.0, 0.05);
	obs_properties_add_bool(props, "automask", obs_module_text("AutoMask"));

	obs_properties_t *advanced = obs_properties_create();
	obs_property_t *ui_corr = obs_properties_add_list(advanced, "ui_correction", obs_module_text("UICorrection"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(ui_corr, obs_module_text("UICorrection.Off"), 0);
	obs_property_list_add_int(ui_corr, obs_module_text("UICorrection.On"), 1);

	obs_properties_add_int(advanced, "gpu_index", obs_module_text("GPUIndex"), 0, 15, 1);

	obs_property_t *chan = obs_properties_add_list(advanced, "channel_order", obs_module_text("ChannelOrder"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(chan, obs_module_text("ChannelOrder.Auto"), DLSSNR_CHANNEL_AUTO);
	obs_property_list_add_int(chan, obs_module_text("ChannelOrder.RGBA"), DLSSNR_CHANNEL_RGBA);
	obs_property_list_add_int(chan, obs_module_text("ChannelOrder.BGRA"), DLSSNR_CHANNEL_BGRA);

	obs_properties_add_group(props, "advanced", obs_module_text("Advanced"), OBS_GROUP_NORMAL, advanced);

	obs_properties_add_button(props, "reset_history", obs_module_text("ResetHistory.Button"),
				  dlss5nr_reset_history_cb);

	return props;
}

static void dlss5nr_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "style", DLSSNR_STYLE_NATURAL);
	obs_data_set_default_int(settings, "preset", 3);
	obs_data_set_default_double(settings, "intensity", 1.0);
	obs_data_set_default_double(settings, "tone", 1.0);
	obs_data_set_default_double(settings, "structure", 1.0);
	obs_data_set_default_double(settings, "skin", -1.0);
	obs_data_set_default_bool(settings, "automask", false);
	obs_data_set_default_int(settings, "ui_correction", 0);
	obs_data_set_default_int(settings, "gpu_index", 0);
	obs_data_set_default_int(settings, "channel_order", DLSSNR_CHANNEL_AUTO);
	obs_data_set_default_string(settings, "status", "");
}

static obs_source_info dlss5nr_filter_info = {
	.id = "obs-dlss5-nr-filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.get_name = dlss5nr_get_name,
	.create = dlss5nr_create,
	.destroy = dlss5nr_destroy,
	.get_width = dlss5nr_width,
	.get_height = dlss5nr_height,
	.get_defaults = dlss5nr_defaults,
	.get_properties = dlss5nr_properties,
	.update = dlss5nr_update,
	.video_render = dlss5nr_video_render,
	.icon_type = OBS_ICON_TYPE_UNKNOWN,
};

void register_dlss5nr_filter(void)
{
	obs_register_source(&dlss5nr_filter_info);
	blog(LOG_INFO, "[obs-dlss5-nr] filter registered");
}
