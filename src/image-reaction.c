//
// Created by scaled
//
// Based on image-source.c from OBS Studio: https://github.com/obsproject/obs-studio
// Also included some code from Spectralizer plugin: https://github.com/univrsal/spectralizer
//

#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <sys/stat.h>
#include <media-io/audio-math.h>
#include <math.h>

#define blog(log_level, format, ...)                             \
	blog(log_level, "[image_reaction_source: '%s'] " format, \
	     obs_source_get_name(context->source), ##__VA_ARGS__)

#define debug(format, ...) blog(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...) blog(LOG_INFO, format, ##__VA_ARGS__)
#define warn(format, ...) blog(LOG_WARNING, format, ##__VA_ARGS__)

enum state_effect {
	EFFECT_NONE,
	EFFECT_VIBE,
	EFFECT_DRIFT,
	EFFECT_SHAKE,
};

struct state_effect_settings {
	enum state_effect type;
	float intensity;
	float speed;
};

/* images: silence, sound, silence while blinking, sound while blinking */
#define IMAGE_COUNT 4

static const char *file_settings[IMAGE_COUNT] = {"file1", "file2",
						 "file1_blink", "file2_blink"};

struct image_reaction_source {
	obs_source_t *source;
	char source_name[255];

	char *files[IMAGE_COUNT];
	bool persistent;
	bool linear_alpha;
	bool active;

	gs_image_file3_t images[IMAGE_COUNT];

	obs_weak_source_t *audio_source;

	bool loud;
	float threshold;
	float smoothness;
	float average;

	uint64_t last_time;
	uint64_t capture_check_time;

	bool animReset1;
	bool animReset2;
	bool loudOld;
	bool animResetTrigger;

	/* index 0 - silence, index 1 - sound */
	struct state_effect_settings effects[2];
	float effect_time;
	float shake_timer;
	uint32_t shake_seed;
	float offset_x;
	float offset_y;
	uint32_t padding;

	bool blinking;
	float blink_timer;
	float blink_interval_min;
	float blink_interval_max;
	float blink_duration;
};

/*int MAX(int a, int b) {
	return a > b ? a : b;
}*/

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const char *image_reaction_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ImageReactionSource");
}

static void image_reaction_source_load(struct image_reaction_source *context)
{
	for (int i = 0; i < IMAGE_COUNT; i++) {
		char *file = context->files[i];
		gs_image_file3_t *if3 = &context->images[i];

		obs_enter_graphics();
		gs_image_file3_free(if3);
		obs_leave_graphics();

		if (file && *file) {
			debug("loading texture '%s'", file);
			gs_image_file3_init(
				if3, file,
				context->linear_alpha
					? GS_IMAGE_ALPHA_PREMULTIPLY_SRGB
					: GS_IMAGE_ALPHA_PREMULTIPLY);

			obs_enter_graphics();
			gs_image_file3_init_texture(if3);
			obs_leave_graphics();

			if (!if3->image2.image.loaded)
				warn("failed to load texture '%s'", file);
		}
	}
}

static void image_reaction_source_unload(struct image_reaction_source *context)
{
	obs_enter_graphics();
	for (int i = 0; i < IMAGE_COUNT; i++)
		gs_image_file3_free(&context->images[i]);
	obs_leave_graphics();
}

static void audio_capture(void *param, obs_source_t *src,
			  const struct audio_data *data, bool muted)
{
	struct image_reaction_source *context = param;
	UNUSED_PARAMETER(src);

	if (muted) {
		context->average = 0;
	} else {
		uint32_t samplesCount = data->frames;
		float *samples = (float *)data->data[0];

		float averageLocal = 0.0f;

		for (uint32_t i = 0; i < samplesCount; i++) {
			averageLocal +=
				(float)(fabs(samples[i])) / samplesCount;
		}

		context->average +=
			context->smoothness * (averageLocal - context->average);
	}

	context->loudOld = context->loud;
	context->loud = context->average > context->threshold;

	if (context->loud != context->loudOld)
		context->animResetTrigger = true;
}

static enum state_effect effect_from_string(const char *name)
{
	if (strcmp(name, "vibe") == 0)
		return EFFECT_VIBE;
	if (strcmp(name, "drift") == 0)
		return EFFECT_DRIFT;
	if (strcmp(name, "shake") == 0)
		return EFFECT_SHAKE;
	return EFFECT_NONE;
}

static void image_reaction_source_update(void *data, obs_data_t *settings)
{
	struct image_reaction_source *context = data;
	const bool anim_reset_1 = obs_data_get_bool(settings, "anim_reset_1");
	const bool anim_reset_2 = obs_data_get_bool(settings, "anim_reset_2");
	const bool unload = obs_data_get_bool(settings, "unload");
	const bool linear_alpha = obs_data_get_bool(settings, "linear_alpha");
	const double threshold = obs_data_get_double(settings, "threshold");
	const double smoothness = obs_data_get_double(settings, "smoothness");

	for (int i = 0; i < IMAGE_COUNT; i++) {
		if (context->files[i])
			bfree(context->files[i]);
		context->files[i] = bstrdup(
			obs_data_get_string(settings, file_settings[i]));
	}

	context->animReset1 = anim_reset_1;
	context->animReset2 = anim_reset_2;

	context->persistent = !unload;
	context->linear_alpha = linear_alpha;
	context->threshold = db_to_mul((float)threshold);
	context->smoothness = (float)pow(0.1, smoothness);

	context->effects[0].type =
		effect_from_string(obs_data_get_string(settings, "effect_1"));
	context->effects[0].intensity =
		(float)obs_data_get_double(settings, "effect_intensity_1");
	context->effects[0].speed =
		(float)obs_data_get_double(settings, "effect_speed_1");
	context->effects[1].type =
		effect_from_string(obs_data_get_string(settings, "effect_2"));
	context->effects[1].intensity =
		(float)obs_data_get_double(settings, "effect_intensity_2");
	context->effects[1].speed =
		(float)obs_data_get_double(settings, "effect_speed_2");

	/* Pad the source by the largest movement so that the image is not
	 * clipped when it moves. Padding is the same for both states to keep
	 * the source size stable. */
	float padding = 0.0f;
	for (int i = 0; i <= 1; i++) {
		if (context->effects[i].type != EFFECT_NONE)
			padding = MAX(padding, context->effects[i].intensity);
	}
	context->padding = (uint32_t)ceilf(padding);

	context->blink_interval_min =
		(float)obs_data_get_double(settings, "blink_interval_min");
	context->blink_interval_max =
		(float)obs_data_get_double(settings, "blink_interval_max");
	context->blink_duration =
		(float)obs_data_get_double(settings, "blink_duration");

	/* Load the image if the source is persistent or showing */
	if (context->persistent || obs_source_showing(context->source))
		image_reaction_source_load(data);
	else
		image_reaction_source_unload(data);

	const char *cfg_source_name =
		obs_data_get_string(settings, "audio_source");

	obs_weak_source_t *old = NULL;

	if (cfg_source_name[0] == '\0') {
		if (context->audio_source) {
			old = context->audio_source;
			context->audio_source = NULL;
		}
		context->source_name[0] = '\0';
	} else {
		if (context->source_name[0] == '\0' ||
		    strcmp(context->source_name, cfg_source_name) != 0) {
			if (context->audio_source) {
				old = context->audio_source;
				context->audio_source = NULL;
			}
			strcpy(context->source_name, cfg_source_name);
			context->capture_check_time =
				os_gettime_ns() - 3000000000;
		}
	}

	if (old) {
		obs_source_t *old_source = obs_weak_source_get_source(old);
		if (old_source) {
			info("Removed audio capture from '%s'",
			     obs_source_get_name(old_source));
			obs_source_remove_audio_capture_callback(
				old_source, audio_capture, context);
			obs_source_release(old_source);
		}
		obs_weak_source_release(old);
	}
}

static void image_reaction_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "unload", false);
	obs_data_set_default_bool(settings, "linear_alpha", false);
	obs_data_set_default_string(settings, "audio_source", "");
	obs_data_set_default_double(settings, "threshold", -40.0f);
	obs_data_set_default_double(settings, "smoothness", 1.0f);
	obs_data_set_default_string(settings, "effect_1", "none");
	obs_data_set_default_double(settings, "effect_intensity_1", 10.0);
	obs_data_set_default_double(settings, "effect_speed_1", 1.0);
	obs_data_set_default_string(settings, "effect_2", "none");
	obs_data_set_default_double(settings, "effect_intensity_2", 10.0);
	obs_data_set_default_double(settings, "effect_speed_2", 1.0);
	obs_data_set_default_double(settings, "blink_interval_min", 2.0);
	obs_data_set_default_double(settings, "blink_interval_max", 6.0);
	obs_data_set_default_double(settings, "blink_duration", 0.15);
}

static void image_reaction_source_show(void *data)
{
	struct image_reaction_source *context = data;

	if (!context->persistent)
		image_reaction_source_load(context);
}

static void image_reaction_source_hide(void *data)
{
	struct image_reaction_source *context = data;

	if (!context->persistent)
		image_reaction_source_unload(context);
}

static void *image_reaction_source_create(obs_data_t *settings,
					  obs_source_t *source)
{
	struct image_reaction_source *context =
		bzalloc(sizeof(struct image_reaction_source));
	context->source = source;

	context->source_name[0] = '\0';
	context->loud = false;

	image_reaction_source_update(context, settings);
	return context;
}

static void image_reaction_source_destroy(void *data)
{
	struct image_reaction_source *context = data;

	image_reaction_source_unload(context);

	for (int i = 0; i < IMAGE_COUNT; i++) {
		if (context->files[i])
			bfree(context->files[i]);
	}

	/*if (context->audio_source) {
		//obs_source_t *source = obs_weak_source_get_source(context->audio_source);
		//if (source) {
			info("Removed audio capture from '%s'", obs_source_get_name(context->audio_source));
			obs_source_remove_audio_capture_callback(context->audio_source, audio_capture, context);
			//obs_source_release(source);
		//}
		//obs_weak_source_release(context->audio_source);
	}*/
	if (context->audio_source) {
		obs_source_t *source =
			obs_weak_source_get_source(context->audio_source);
		if (source) {
			info("Removed audio capture from '%s'",
			     obs_source_get_name(source));
			obs_source_remove_audio_capture_callback(
				source, audio_capture, context);
			obs_source_release(source);
		}
		obs_weak_source_release(context->audio_source);
	}

	bfree(context);
}

static uint32_t image_reaction_source_getwidth(void *data)
{
	struct image_reaction_source *context = data;
	uint32_t cx = 0;
	for (int i = 0; i < IMAGE_COUNT; i++)
		cx = MAX(cx, context->images[i].image2.image.cx);
	return cx ? cx + context->padding * 2 : 0;
}

static uint32_t image_reaction_source_getheight(void *data)
{
	struct image_reaction_source *context = data;
	uint32_t cy = 0;
	for (int i = 0; i < IMAGE_COUNT; i++)
		cy = MAX(cy, context->images[i].image2.image.cy);
	return cy ? cy + context->padding * 2 : 0;
}

static void image_reaction_source_render(void *data, gs_effect_t *effect)
{
	struct image_reaction_source *context = data;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	/* Use the blink image if there is one for the current state */
	int index = context->loud ? 1 : 0;
	if (context->blinking &&
	    context->images[index + 2].image2.image.texture)
		index += 2;

	gs_image_file3_t *if3 = &context->images[index];
	if (if3->image2.image.texture) {
		gs_eparam_t *const param =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture_srgb(param, if3->image2.image.texture);

		gs_matrix_push();
		gs_matrix_translate3f((float)context->padding + context->offset_x,
				      (float)context->padding + context->offset_y,
				      0.0f);
		gs_draw_sprite(if3->image2.image.texture, 0,
			       if3->image2.image.cx, if3->image2.image.cy);
		gs_matrix_pop();
	}
	//context->loud = false;

	gs_blend_state_pop();

	gs_enable_framebuffer_srgb(previous);
}

static float shake_random(struct image_reaction_source *context)
{
	context->shake_seed = context->shake_seed * 1664525u + 1013904223u;
	return (float)(context->shake_seed >> 8) / 8388608.0f - 1.0f;
}

static void image_reaction_update_effect(struct image_reaction_source *context,
					 float seconds)
{
	const struct state_effect_settings *effect =
		&context->effects[context->loud ? 1 : 0];
	const float intensity = effect->intensity;

	context->effect_time += seconds * effect->speed;
	if (context->effect_time > 3600.0f)
		context->effect_time -= 3600.0f;
	const float t = context->effect_time * 2.0f * (float)M_PI;

	switch (effect->type) {
	case EFFECT_VIBE:
		/* rhythmic bob with a slight sway */
		context->offset_x = intensity * 0.35f * sinf(t);
		context->offset_y = intensity * sinf(t * 2.0f);
		break;
	case EFFECT_DRIFT:
		/* slow wandering, sines with unrelated periods */
		context->offset_x = intensity * (0.6f * sinf(t * 0.17f) +
						 0.4f * sinf(t * 0.41f + 1.3f));
		context->offset_y = intensity * (0.6f * sinf(t * 0.23f + 2.1f) +
						 0.4f * sinf(t * 0.13f + 0.7f));
		break;
	case EFFECT_SHAKE:
		/* new random position 30 times per second */
		context->shake_timer += seconds * effect->speed;
		if (context->shake_timer >= 1.0f / 30.0f) {
			context->shake_timer = 0.0f;
			context->offset_x = intensity * shake_random(context);
			context->offset_y = intensity * shake_random(context);
		}
		break;
	default:
		context->offset_x = 0.0f;
		context->offset_y = 0.0f;
	}
}

static void image_reaction_update_blink(struct image_reaction_source *context,
					float seconds)
{
	context->blink_timer -= seconds;
	if (context->blink_timer > 0.0f)
		return;

	if (context->blinking) {
		/* eyes open until the next blink, after a random interval */
		const float min = context->blink_interval_min;
		const float max = MAX(min, context->blink_interval_max);
		const float random = (shake_random(context) + 1.0f) * 0.5f;

		context->blinking = false;
		context->blink_timer = min + (max - min) * random;
	} else {
		context->blinking = true;
		context->blink_timer = context->blink_duration;
	}
}

static void image_reaction_tick(void *data, float seconds)
{
	struct image_reaction_source *context = data;

	image_reaction_update_effect(context, seconds);
	image_reaction_update_blink(context, seconds);

	// Update / refresh audio capturing
	char *new_name = NULL;
	if (context->source_name[0] != '\0' && !context->audio_source) {
		uint64_t t = os_gettime_ns();

		if (t - context->capture_check_time > 3000000000) {
			new_name = context->source_name;
			context->capture_check_time = t;
		}
	}

	if (new_name != NULL) {
		obs_source_t *capture = obs_get_source_by_name(new_name);
		obs_weak_source_t *weak_capture =
			capture ? obs_source_get_weak_source(capture) : NULL;

		if (context->source_name[0] != '\0' &&
		    new_name == context->source_name) {
			context->audio_source = weak_capture;
			weak_capture = NULL;
		}

		if (capture) {
			info("Added audio capture to '%s'",
			     obs_source_get_name(capture));
			obs_source_add_audio_capture_callback(
				capture, audio_capture, context);
			obs_weak_source_release(weak_capture);
			obs_source_release(capture);
		}
	}

	// update GIF's
	uint64_t frame_time = obs_get_video_frame_time();
	if (obs_source_active(context->source)) {
		if (!context->active) {
			for (int i = 0; i < IMAGE_COUNT; i++) {
				if (context->images[i]
					    .image2.image.is_animated_gif)
					context->last_time = frame_time;
			}
			context->active = true;
		}

	} else {
		if (context->active) {
			for (int i = 0; i < IMAGE_COUNT; i++) {
				gs_image_file3_t *if3 = &context->images[i];
				if (if3->image2.image.is_animated_gif) {
					if3->image2.image.cur_frame = 0;
					if3->image2.image.cur_loop = 0;
					if3->image2.image.cur_time = 0;

					obs_enter_graphics();
					gs_image_file3_update_texture(if3);
					obs_leave_graphics();
				}
			}

			context->active = false;
		}
	}

	for (int i = 0; i < IMAGE_COUNT; i++) {
		gs_image_file3_t *if3 = &context->images[i];
		bool animReset = i % 2 == 0 ? context->animReset1
					    : context->animReset2;

		if (context->last_time && if3->image2.image.is_animated_gif) {
			if (animReset && context->animResetTrigger) {
				if3->image2.image.cur_frame = 0;
				if3->image2.image.cur_loop = 0;
				if3->image2.image.cur_time = 0;

				obs_enter_graphics();
				gs_image_file3_update_texture(if3);
				obs_leave_graphics();
			} else {
				uint64_t elapsed =
					frame_time - context->last_time;
				bool updated =
					gs_image_file3_tick(if3, elapsed);

				if (updated) {
					obs_enter_graphics();
					gs_image_file3_update_texture(if3);
					obs_leave_graphics();
				}
			}
		}
	}
	context->animResetTrigger = false;

	context->last_time = frame_time;
}

static const char *image_filter =
	"All formats (*.bmp *.tga *.png *.jpeg *.jpg *.gif *.psd *.webp);;"
	"BMP Files (*.bmp);;"
	"Targa Files (*.tga);;"
	"PNG Files (*.png);;"
	"JPEG Files (*.jpeg *.jpg);;"
	"GIF Files (*.gif);;"
	"PSD Files (*.psd);;"
	"WebP Files (*.webp);;"
	"All Files (*.*)";

static bool add_source(void *param, obs_source_t *src)
{
	obs_property_t *list = param;

	uint32_t caps = obs_source_get_output_flags(src);

	if ((caps & OBS_SOURCE_AUDIO) == 0)
		return true;
	const char *name = obs_source_get_name(src);
	obs_property_list_add_string(list, name, name);
	return true;
}

static bool source_changed(obs_properties_t *props, obs_property_t *prop,
			   obs_data_t *data)
{
	obs_data_get_string(data, "audio_source");
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(prop);
	return true;
}

static bool effect_changed(obs_properties_t *props, obs_property_t *prop,
			   obs_data_t *settings)
{
	UNUSED_PARAMETER(prop);

	const bool enabled1 = effect_from_string(obs_data_get_string(
				      settings, "effect_1")) != EFFECT_NONE;
	const bool enabled2 = effect_from_string(obs_data_get_string(
				      settings, "effect_2")) != EFFECT_NONE;

	obs_property_set_visible(
		obs_properties_get(props, "effect_intensity_1"), enabled1);
	obs_property_set_visible(obs_properties_get(props, "effect_speed_1"),
				 enabled1);
	obs_property_set_visible(
		obs_properties_get(props, "effect_intensity_2"), enabled2);
	obs_property_set_visible(obs_properties_get(props, "effect_speed_2"),
				 enabled2);
	return true;
}

static bool blink_changed(obs_properties_t *props, obs_property_t *prop,
			  obs_data_t *settings)
{
	UNUSED_PARAMETER(prop);

	const bool enabled =
		*obs_data_get_string(settings, "file1_blink") != '\0' ||
		*obs_data_get_string(settings, "file2_blink") != '\0';

	obs_property_set_visible(
		obs_properties_get(props, "blink_interval_min"), enabled);
	obs_property_set_visible(
		obs_properties_get(props, "blink_interval_max"), enabled);
	obs_property_set_visible(obs_properties_get(props, "blink_duration"),
				 enabled);
	return true;
}

static void add_effect_properties(obs_properties_t *props, const char *effect,
				  const char *effect_text,
				  const char *intensity, const char *speed)
{
	obs_property_t *p = obs_properties_add_list(props, effect,
						    obs_module_text(effect_text),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("Effect.None"), "none");
	obs_property_list_add_string(p, obs_module_text("Effect.Vibe"), "vibe");
	obs_property_list_add_string(p, obs_module_text("Effect.Drift"),
				     "drift");
	obs_property_list_add_string(p, obs_module_text("Effect.Shake"),
				     "shake");
	obs_property_set_modified_callback(p, effect_changed);

	p = obs_properties_add_float_slider(props, intensity,
					    obs_module_text("EffectIntensity"),
					    1.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " px");

	p = obs_properties_add_float_slider(props, speed,
					    obs_module_text("EffectSpeed"), 0.1,
					    5.0, 0.1);
	obs_property_float_set_suffix(p, "x");
}

static obs_properties_t *image_reaction_source_properties(void *data)
{
	struct image_reaction_source *s = data;
	struct dstr path = {0};

	obs_properties_t *props = obs_properties_create();

	if (s && s->files[0] && *s->files[0]) {
		const char *slash;

		dstr_copy(&path, s->files[0]);
		dstr_replace(&path, "\\", "/");
		slash = strrchr(path.array, '/');
		if (slash)
			dstr_resize(&path, slash - path.array + 1);
	}

	obs_properties_add_path(props, "file1", obs_module_text("Reaction1"),
				OBS_PATH_FILE, image_filter, path.array);
	obs_property_t *p = obs_properties_add_path(props, "file1_blink",
						    obs_module_text("Blink1"),
						    OBS_PATH_FILE, image_filter,
						    path.array);
	obs_property_set_modified_callback(p, blink_changed);
	obs_properties_add_bool(props, "anim_reset_1",
				obs_module_text("AnimReset1"));
	add_effect_properties(props, "effect_1", "Effect1",
			      "effect_intensity_1", "effect_speed_1");
	obs_properties_add_path(props, "file2", obs_module_text("Reaction2"),
				OBS_PATH_FILE, image_filter, path.array);
	p = obs_properties_add_path(props, "file2_blink",
				    obs_module_text("Blink2"), OBS_PATH_FILE,
				    image_filter, path.array);
	obs_property_set_modified_callback(p, blink_changed);
	obs_properties_add_bool(props, "anim_reset_2",
				obs_module_text("AnimReset2"));
	add_effect_properties(props, "effect_2", "Effect2",
			      "effect_intensity_2", "effect_speed_2");
	dstr_free(&path);

	p = obs_properties_add_float_slider(props, "blink_interval_min",
					    obs_module_text("BlinkIntervalMin"),
					    0.5, 30.0, 0.1);
	obs_property_float_set_suffix(p, " s");
	p = obs_properties_add_float_slider(props, "blink_interval_max",
					    obs_module_text("BlinkIntervalMax"),
					    0.5, 30.0, 0.1);
	obs_property_float_set_suffix(p, " s");
	p = obs_properties_add_float_slider(props, "blink_duration",
					    obs_module_text("BlinkDuration"),
					    0.05, 1.0, 0.01);
	obs_property_float_set_suffix(p, " s");

	obs_properties_add_bool(props, "unload",
				obs_module_text("UnloadWhenNotShowing"));
	obs_properties_add_bool(props, "linear_alpha",
				obs_module_text("LinearAlpha"));
	obs_property_t *sources_list = obs_properties_add_list(
		props, "audio_source", obs_module_text("AudioSource"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(sources_list, "", "");

	p = obs_properties_add_float_slider(
		props, "threshold", obs_module_text("Threshold"), -60.0, 0.0,
		0.1);
	obs_property_float_set_suffix(p, " dB");

	obs_properties_add_float_slider(props, "smoothness",
					obs_module_text("Smoothness"), 0.0, 5.0,
					0.1);

	//obs_property_set_modified_callback(src, source_changed);
	obs_enum_sources(add_source, sources_list);

	return props;
}

uint64_t image_reaction_source_get_memory_usage(void *data)
{
	struct image_reaction_source *s = data;
	uint64_t mem_usage = 0;
	for (int i = 0; i < IMAGE_COUNT; i++)
		mem_usage += s->images[i].image2.mem_usage;
	return mem_usage;
}

static void missing_file_callback(void *src, const char *new_path, void *data)
{
	struct image_reaction_source *s = src;

	obs_source_t *source = s->source;
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "file", new_path);
	obs_source_update(source, settings);
	obs_data_release(settings);

	UNUSED_PARAMETER(data);
}

static struct obs_source_info image_reaction_source_info = {
	.id = "image_reaction_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = image_reaction_source_get_name,
	.create = image_reaction_source_create,
	.destroy = image_reaction_source_destroy,
	.update = image_reaction_source_update,
	.get_defaults = image_reaction_source_defaults,
	.show = image_reaction_source_show,
	.hide = image_reaction_source_hide,
	.get_width = image_reaction_source_getwidth,
	.get_height = image_reaction_source_getheight,
	.video_render = image_reaction_source_render,
	.video_tick = image_reaction_tick,
	.get_properties = image_reaction_source_properties,
	.icon_type = OBS_ICON_TYPE_IMAGE,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("image-reaction", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Image reaction source";
}

extern struct obs_source_info slideshow_info;

bool obs_module_load(void)
{
	obs_register_source(&image_reaction_source_info);
	return true;
}
