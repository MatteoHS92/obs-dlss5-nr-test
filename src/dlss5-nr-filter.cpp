// obs-dlss5-nr — DLSS 5 Neural Rendering filter for OBS Studio
// Copyright (C) 2026 Saganaki22
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Phase 1: pass-through video filter with the full DLSS 5 NR properties UI.
// The NGX/D3D12 bridge lands in Phase 2; until then the filter copies its
// input to the output unchanged and the style/settings widgets only record
// state.

#include "dlss5-nr-filter.h"

#include <obs-module.h>
#include <util/base.h>

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

struct dlss5nr_filter {
	obs_source_t *context;
	gs_texrender_t *texrender;

	// Mirrored settings (consumed by the bridge in Phase 2).
	int style;
	int preset;
	float intensity;
	float tone;
	float structure;
	float skin;
	bool automask;
	int ui_correction;
	int gpu_index;
	int channel_order;
	bool reset_pending;
	bool initialized;
};

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
	f->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

	if (!f->texrender) {
		blog(LOG_ERROR, "[obs-dlss5-nr] failed to create texrender");
		delete f;
		return nullptr;
	}

	obs_source_update(context, settings);
	return f;
}

static void dlss5nr_destroy(void *data)
{
	auto *f = static_cast<dlss5nr_filter *>(data);
	if (!f)
		return;

	if (f->texrender) {
		obs_enter_graphics();
		gs_texrender_destroy(f->texrender);
		obs_leave_graphics();
	}
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

static void dlss5nr_video_render(void *data, gs_effect_t *filter_effect)
{
	UNUSED_PARAMETER(filter_effect);
	auto *f = static_cast<dlss5nr_filter *>(data);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	if (!parent || !f->texrender) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	const uint32_t cx = obs_source_get_base_width(parent);
	const uint32_t cy = obs_source_get_base_height(parent);
	if (cx == 0 || cy == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	// Phase 1 pass-through: grab the parent's rendered frame into our own
	// texture (Phase 2 will feed this to the DLSS NR bridge) and draw it
	// back unchanged.
	gs_texrender_reset(f->texrender);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (!gs_texrender_begin(f->texrender, cx, cy)) {
		gs_blend_state_pop();
		obs_source_skip_video_filter(f->context);
		return;
	}

	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	obs_source_video_render(parent);

	gs_texrender_end(f->texrender);
	gs_blend_state_pop();

	gs_texture_t *tex = gs_texrender_get_texture(f->texrender);
	if (!tex) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, tex);

	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, cx, cy);
}

static void dlss5nr_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<dlss5nr_filter *>(data);

	const long long new_style = obs_data_get_int(settings, "style");
	if (f->initialized && new_style != f->style) {
		blog(LOG_INFO,
		     "[obs-dlss5-nr] style changed to %lld — NR feature will be rebuilt (bridge lands in Phase 2)",
		     new_style);
	}
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
	blog(LOG_INFO, "[obs-dlss5-nr] history reset requested (applied by the bridge in Phase 2)");
	return false;
}

static obs_properties_t *dlss5nr_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *status = obs_properties_add_text(props, "status", obs_module_text("Status"), OBS_TEXT_INFO);
	obs_property_text_set_info_type(status, OBS_TEXT_INFO_NORMAL);
	obs_property_set_long_description(status, obs_module_text("Status.Phase1"));

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
