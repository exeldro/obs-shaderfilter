// Version 2.0 by Exeldro https://github.com/exeldro/obs-shaderfilter
// Version 1.21 by Charles Fettinger https://github.com/Oncorporation/obs-shaderfilter
// original version by nleseul https://github.com/nleseul/obs-shaderfilter
#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/math-extra.h>

#include <util/base.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>

#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <util/threading.h>

#include "version.h"

#define nullptr ((void *)0)

static const char *effect_template_begin = "\
uniform float4x4 ViewProj;\n\
uniform texture2d image;\n\
\n\
uniform float2 uv_offset;\n\
uniform float2 uv_scale;\n\
uniform float2 uv_pixel_interval;\n\
uniform float2 uv_size;\n\
uniform float rand_f;\n\
uniform float rand_instance_f;\n\
uniform float rand_activation_f;\n\
uniform float elapsed_time;\n\
uniform int loops;\n\
uniform float local_time;\n\
\n\
sampler_state textureSampler{\n\
	Filter = Linear;\n\
	AddressU = Border;\n\
	AddressV = Border;\n\
	BorderColor = 00000000;\n\
};\n\
\n\
struct VertData {\n\
	float4 pos : POSITION;\n\
	float2 uv : TEXCOORD0;\n\
};\n\
\n\
VertData mainTransform(VertData v_in)\n\
{\n\
	VertData vert_out;\n\
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);\n\
	vert_out.uv = v_in.uv * uv_scale + uv_offset;\n\
	return vert_out;\n\
}\n\
\n\
float srgb_nonlinear_to_linear_channel(float u)\n\
{\n\
	return (u <= 0.04045) ? (u / 12.92) : pow((u + 0.055) / 1.055, 2.4);\n\
}\n\
\n\
float3 srgb_nonlinear_to_linear(float3 v)\n\
{\n\
	return float3(srgb_nonlinear_to_linear_channel(v.r),\n\
		      srgb_nonlinear_to_linear_channel(v.g),\n\
		      srgb_nonlinear_to_linear_channel(v.b));\n\
}\n\
\n";

static const char *effect_template_default_image_shader = "\n\
float4 mainImage(VertData v_in) : TARGET\n\
{\n\
	return image.Sample(textureSampler, v_in.uv);\n\
}\n\
";

static const char *effect_template_default_transition_image_shader = "\n\
uniform texture2d image_a;\n\
uniform texture2d image_b;\n\
uniform float transition_time = 0.5;\n\
uniform bool convert_linear = true;\n\
\n\
float4 mainImage(VertData v_in) : TARGET\n\
{\n\
	float4 a_val = image_a.Sample(textureSampler, v_in.uv);\n\
	float4 b_val = image_b.Sample(textureSampler, v_in.uv);\n\
	float4 rgba = lerp(a_val, b_val, transition_time);\n\
	if (convert_linear)\n\
		rgba.rgb = srgb_nonlinear_to_linear(rgba.rgb);\n\
	return rgba;\n\
}\n\
";

static const char *effect_template_end = "\n\
technique Draw\n\
{\n\
	pass\n\
	{\n\
		vertex_shader = mainTransform(v_in);\n\
		pixel_shader = mainImage(v_in);\n\
	}\n\
}\n";

struct effect_param_data {
	struct dstr name;
	struct dstr display_name;
	struct dstr widget_type;
	struct dstr description;
	struct dstr group;
	struct dstr path;
	DARRAY(int) option_values;
	DARRAY(struct dstr) option_labels;

	enum gs_shader_param_type type;
	gs_eparam_t *param;

	gs_image_file_t *image;
	gs_texrender_t *render;
	obs_weak_source_t *source;

	union {
		long long i;
		double f;
		char *string;
		struct vec2 vec2;
		struct vec3 vec3;
		struct vec4 vec4;
	} value;
	char *label;
	union {
		long long i;
		double f;
	} minimum;
	union {
		long long i;
		double f;
	} maximum;
	union {
		long long i;
		double f;
	} step;
};

struct shader_filter_data {
	obs_source_t *context;
	gs_effect_t *effect;
	gs_effect_t *output_effect;

	gs_texrender_t *input_texrender;
	gs_texrender_t *output_texrender;
	gs_eparam_t *param_output_image;

	bool reload_effect;
	struct dstr last_path;
	bool last_from_file;
	bool transition;
	bool transitioning;
	bool prev_transitioning;

	bool use_pm_alpha;
	bool rendered;

	float shader_start_time;

	gs_eparam_t *param_uv_offset;
	gs_eparam_t *param_uv_scale;
	gs_eparam_t *param_uv_pixel_interval;
	gs_eparam_t *param_uv_size;
	gs_eparam_t *param_elapsed_time;
	gs_eparam_t *param_loops;
	gs_eparam_t *param_local_time;
	gs_eparam_t *param_rand_f;
	gs_eparam_t *param_rand_instance_f;
	gs_eparam_t *param_rand_activation_f;
	gs_eparam_t *param_image;
	gs_eparam_t *param_image_a;
	gs_eparam_t *param_image_b;
	gs_eparam_t *param_transition_time;
	gs_eparam_t *param_convert_linear;

	int expand_left;
	int expand_right;
	int expand_top;
	int expand_bottom;

	int total_width;
	int total_height;
	bool use_shader_elapsed_time;
	bool no_repeat;
	bool rendering;

	struct vec2 uv_offset;
	struct vec2 uv_scale;
	struct vec2 uv_pixel_interval;
	struct vec2 uv_size;
	float elapsed_time;
	float elapsed_time_loop;
	int loops;
	float local_time;
	float rand_f;
	float rand_instance_f;
	float rand_activation_f;

	DARRAY(struct effect_param_data) stored_param_list;
};

static unsigned int rand_interval(unsigned int min, unsigned int max)
{
	unsigned int r;
	const unsigned int range = 1 + max - min;
	const unsigned int buckets = RAND_MAX / range;
	const unsigned int limit = buckets * range;

	/* Create equal size buckets all in a row, then fire randomly towards
	 * the buckets until you land in one of them. All buckets are equally
	 * likely. If you land off the end of the line of buckets, try again. */
	do {
		r = rand();
	} while (r >= limit);

	return min + (r / buckets);
}

static char *load_shader_from_file(const char *file_name) // add input of visited files
{

	char *file_ptr = os_quick_read_utf8_file(file_name);
	if (file_ptr == NULL)
		return NULL;
	char **lines = strlist_split(file_ptr, '\n', true);
	struct dstr shader_file;
	dstr_init(&shader_file);

	size_t line_i = 0;
	while (lines[line_i] != NULL) {
		char *line = lines[line_i];
		line_i++;
		if (strncmp(line, "#include", 8) == 0) {
			// Open the included file, place contents here.
			char *pos = strrchr(file_name, '/');
			const size_t length = pos - file_name + 1;
			struct dstr include_path = {0};
			dstr_ncopy(&include_path, file_name, length);
			char *start = strchr(line, '"') + 1;
			char *end = strrchr(line, '"');

			dstr_ncat(&include_path, start, end - start);
			char *abs_include_path = os_get_abs_path_ptr(include_path.array);
			char *file_contents = load_shader_from_file(abs_include_path);
			dstr_cat(&shader_file, file_contents);
			dstr_cat(&shader_file, "\n");
			bfree(abs_include_path);
			bfree(file_contents);
			dstr_free(&include_path);
		} else {
			// else place current line here.
			dstr_cat(&shader_file, line);
			dstr_cat(&shader_file, "\n");
		}
	}

	// Add file_name to visited files
	// Do stuff with the file, and populate shader_file
	/*

		for line in file:
		   if line starts with #include
		       get path
		       if path is not in visited files
			   include_file_contents = load_shader_from_file(path)
	                   concat include_file_contents onto shader_file
	           else
		       concat line onto shader_file
	*/
	bfree(file_ptr);
	strlist_free(lines);
	return shader_file.array;
}

static void shader_filter_clear_params(struct shader_filter_data *filter)
{
	filter->param_elapsed_time = NULL;
	filter->param_uv_offset = NULL;
	filter->param_uv_pixel_interval = NULL;
	filter->param_uv_scale = NULL;
	filter->param_uv_size = NULL;
	filter->param_rand_f = NULL;
	filter->param_rand_activation_f = NULL;
	filter->param_rand_instance_f = NULL;
	filter->param_loops = NULL;
	filter->param_local_time = NULL;
	filter->param_image = NULL;
	filter->param_image_a = NULL;
	filter->param_image_b = NULL;
	filter->param_transition_time = NULL;
	filter->param_convert_linear = NULL;

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param = (filter->stored_param_list.array + param_index);
		if (param->image) {
			obs_enter_graphics();
			gs_image_file_free(param->image);
			obs_leave_graphics();

			bfree(param->image);
			param->image = NULL;
		}
		if (param->source) {
			obs_source_t *source = obs_weak_source_get_source(param->source);
			if (source) {
				if ((!filter->transition || filter->prev_transitioning) && obs_source_active(filter->context))
					obs_source_dec_active(source);
				if ((!filter->transition || filter->prev_transitioning) && obs_source_showing(filter->context))
					obs_source_dec_showing(source);
				obs_source_release(source);
			}
			obs_weak_source_release(param->source);
			param->source = NULL;
		}
		if (param->render) {
			obs_enter_graphics();
			gs_texrender_destroy(param->render);
			obs_leave_graphics();
			param->render = NULL;
		}
		dstr_free(&param->name);
		dstr_free(&param->display_name);
		dstr_free(&param->widget_type);
		dstr_free(&param->description);
		dstr_free(&param->group);
		dstr_free(&param->path);
		da_free(param->option_values);
		for (size_t i = 0; i < param->option_labels.num; i++) {
			dstr_free(&param->option_labels.array[i]);
		}
		da_free(param->option_labels);
	}

	da_free(filter->stored_param_list);
}

static void load_output_effect(struct shader_filter_data *filter)
{
	if (filter->output_effect != NULL) {
		obs_enter_graphics();
		gs_effect_destroy(filter->output_effect);
		filter->output_effect = NULL;
		obs_leave_graphics();
	}

	char *shader_text = NULL;
	struct dstr filename = {0};
	dstr_cat(&filename, obs_get_module_data_path(obs_current_module()));
	dstr_cat(&filename, "/internal/render_output.effect");
	shader_text = load_shader_from_file(filename.array);
	char *errors = NULL;
	dstr_free(&filename);

	obs_enter_graphics();
	filter->output_effect = gs_effect_create(shader_text, NULL, &errors);
	obs_leave_graphics();

	bfree(shader_text);
	if (filter->output_effect == NULL) {
		blog(LOG_WARNING, "[obs-shaderfilter] Unable to load output.effect file.  Errors:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)" : errors));
		bfree(errors);
	} else {
		size_t effect_count = gs_effect_get_num_params(filter->output_effect);
		for (size_t effect_index = 0; effect_index < effect_count; effect_index++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(filter->output_effect, effect_index);
			struct gs_effect_param_info info;
			gs_effect_get_param_info(param, &info);
			if (strcmp(info.name, "output_image") == 0) {
				filter->param_output_image = param;
			}
		}
	}
}

static void shader_filter_reload_effect(struct shader_filter_data *filter)
{
	obs_data_t *settings = obs_source_get_settings(filter->context);

	// First, clean up the old effect and all references to it.
	filter->shader_start_time = 0.0;
	shader_filter_clear_params(filter);

	if (filter->effect != NULL) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		filter->effect = NULL;
		obs_leave_graphics();
	}

	// Load text and build the effect from the template, if necessary.
	char *shader_text = NULL;
	bool use_template = !obs_data_get_bool(settings, "override_entire_effect");

	if (obs_data_get_bool(settings, "from_file")) {
		const char *file_name = obs_data_get_string(settings, "shader_file_name");
		shader_text = load_shader_from_file(file_name);
	} else {
		shader_text = bstrdup(obs_data_get_string(settings, "shader_text"));
		use_template = true;
	}

	struct dstr effect_text = {0};

	if (use_template) {
		dstr_cat(&effect_text, effect_template_begin);
	}

	if (shader_text) {
		dstr_cat(&effect_text, shader_text);
		bfree(shader_text);
	}

	if (use_template) {
		dstr_cat(&effect_text, effect_template_end);
	}

	// Create the effect.
	char *errors = NULL;

	obs_enter_graphics();
	int device_type = gs_get_device_type();
	if (device_type == GS_DEVICE_OPENGL) {
		dstr_replace(&effect_text, "[loop]", "");
		dstr_insert(&effect_text, 0, "#define OPENGL 1\n");
	}

	if (effect_text.len && dstr_find(&effect_text, "#define USE_PM_ALPHA 1")) {
		filter->use_pm_alpha = true;
	} else {
		filter->use_pm_alpha = false;
	}

	if (filter->effect)
		gs_effect_destroy(filter->effect);
	filter->effect = gs_effect_create(effect_text.array, NULL, &errors);
	obs_leave_graphics();

	if (filter->effect == NULL) {
		blog(LOG_WARNING, "[obs-shaderfilter] Unable to create effect. Errors returned from parser:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)" : errors));
		if (errors && strlen(errors)) {
			obs_data_set_string(settings, "last_error", errors);
		} else {
			obs_data_set_string(settings, "last_error", obs_module_text("ShaderFilter.Unknown"));
		}
		dstr_free(&effect_text);
		bfree(errors);
		goto end;
	} else {
		dstr_free(&effect_text);
		obs_data_unset_user_value(settings, "last_error");
	}

	// Store references to the new effect's parameters.
	da_free(filter->stored_param_list);

	size_t effect_count = gs_effect_get_num_params(filter->effect);
	for (size_t effect_index = 0; effect_index < effect_count; effect_index++) {
		gs_eparam_t *param = gs_effect_get_param_by_idx(filter->effect, effect_index);

		struct gs_effect_param_info info;
		gs_effect_get_param_info(param, &info);

		if (strcmp(info.name, "uv_offset") == 0) {
			filter->param_uv_offset = param;
		} else if (strcmp(info.name, "uv_scale") == 0) {
			filter->param_uv_scale = param;
		} else if (strcmp(info.name, "uv_pixel_interval") == 0) {
			filter->param_uv_pixel_interval = param;
		} else if (strcmp(info.name, "uv_size") == 0) {
			filter->param_uv_size = param;
		} else if (strcmp(info.name, "elapsed_time") == 0) {
			filter->param_elapsed_time = param;
		} else if (strcmp(info.name, "rand_f") == 0) {
			filter->param_rand_f = param;
		} else if (strcmp(info.name, "rand_activation_f") == 0) {
			filter->param_rand_activation_f = param;
		} else if (strcmp(info.name, "rand_instance_f") == 0) {
			filter->param_rand_instance_f = param;
		} else if (strcmp(info.name, "loops") == 0) {
			filter->param_loops = param;
		} else if (strcmp(info.name, "local_time") == 0) {
			filter->param_local_time = param;
		} else if (strcmp(info.name, "ViewProj") == 0) {
			// Nothing.
		} else if (strcmp(info.name, "image") == 0) {
			filter->param_image = param;
		} else if (filter->transition && strcmp(info.name, "image_a") == 0) {
			filter->param_image_a = param;
		} else if (filter->transition && strcmp(info.name, "image_b") == 0) {
			filter->param_image_b = param;
		} else if (filter->transition && strcmp(info.name, "transition_time") == 0) {
			filter->param_transition_time = param;
		} else if (filter->transition && strcmp(info.name, "convert_linear") == 0) {
			filter->param_convert_linear = param;
		} else {
			struct effect_param_data *cached_data = da_push_back_new(filter->stored_param_list);
			dstr_copy(&cached_data->name, info.name);
			cached_data->type = info.type;
			cached_data->param = param;
			da_init(cached_data->option_values);
			da_init(cached_data->option_labels);
			const size_t annotation_count = gs_param_get_num_annotations(param);
			for (size_t annotation_index = 0; annotation_index < annotation_count; annotation_index++) {
				gs_eparam_t *annotation = gs_param_get_annotation_by_idx(param, annotation_index);
				void *annotation_default = gs_effect_get_default_val(annotation);
				gs_effect_get_param_info(annotation, &info);
				if (strcmp(info.name, "name") == 0 && info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->display_name, (const char *)annotation_default);
				} else if (strcmp(info.name, "label") == 0 && info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->display_name, (const char *)annotation_default);
				} else if (strcmp(info.name, "widget_type") == 0 && info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->widget_type, (const char *)annotation_default);
				} else if (strcmp(info.name, "group") == 0 && info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->group, (const char *)annotation_default);
				} else if (strcmp(info.name, "minimum") == 0) {
					if (info.type == GS_SHADER_PARAM_FLOAT || info.type == GS_SHADER_PARAM_VEC2 ||
					    info.type == GS_SHADER_PARAM_VEC3 || info.type == GS_SHADER_PARAM_VEC4) {
						cached_data->minimum.f = *(float *)annotation_default;
					} else if (info.type == GS_SHADER_PARAM_INT) {
						cached_data->minimum.i = *(int *)annotation_default;
					}
				} else if (strcmp(info.name, "maximum") == 0) {
					if (info.type == GS_SHADER_PARAM_FLOAT || info.type == GS_SHADER_PARAM_VEC2 ||
					    info.type == GS_SHADER_PARAM_VEC3 || info.type == GS_SHADER_PARAM_VEC4) {
						cached_data->maximum.f = *(float *)annotation_default;
					} else if (info.type == GS_SHADER_PARAM_INT) {
						cached_data->maximum.i = *(int *)annotation_default;
					}
				} else if (strcmp(info.name, "step") == 0) {
					if (info.type == GS_SHADER_PARAM_FLOAT || info.type == GS_SHADER_PARAM_VEC2 ||
					    info.type == GS_SHADER_PARAM_VEC3 || info.type == GS_SHADER_PARAM_VEC4) {
						cached_data->step.f = *(float *)annotation_default;
					} else if (info.type == GS_SHADER_PARAM_INT) {
						cached_data->step.i = *(int *)annotation_default;
					}
				} else if (strncmp(info.name, "option_", 7) == 0) {
					int id = atoi(info.name + 7);
					if (info.type == GS_SHADER_PARAM_INT) {
						int val = *(int *)annotation_default;
						int *cd = da_insert_new(cached_data->option_values, id);
						*cd = val;

					} else if (info.type == GS_SHADER_PARAM_STRING) {
						struct dstr val = {0};
						dstr_copy(&val, (const char *)annotation_default);
						struct dstr *cs = da_insert_new(cached_data->option_labels, id);
						*cs = val;
					}
				}
				bfree(annotation_default);
			}
		}
	}

end:
	obs_data_release(settings);
}

static const char *shader_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ShaderFilter");
}

static void *shader_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct shader_filter_data *filter = bzalloc(sizeof(struct shader_filter_data));
	filter->context = source;
	filter->reload_effect = true;

	dstr_init(&filter->last_path);
	dstr_copy(&filter->last_path, obs_data_get_string(settings, "shader_file_name"));
	filter->last_from_file = obs_data_get_bool(settings, "from_file");
	filter->rand_instance_f = (float)((double)rand_interval(0, 10000) / (double)10000);
	filter->rand_activation_f = (float)((double)rand_interval(0, 10000) / (double)10000);

	da_init(filter->stored_param_list);
	load_output_effect(filter);
	obs_source_update(source, settings);

	return filter;
}

static void shader_filter_destroy(void *data)
{
	struct shader_filter_data *filter = data;
	shader_filter_clear_params(filter);

	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	if (filter->output_effect)
		gs_effect_destroy(filter->output_effect);
	if (filter->input_texrender)
		gs_texrender_destroy(filter->input_texrender);
	if (filter->output_texrender)
		gs_texrender_destroy(filter->output_texrender);

	obs_leave_graphics();

	dstr_free(&filter->last_path);
	da_free(filter->stored_param_list);

	bfree(filter);
}

static bool shader_filter_from_file_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);

	bool from_file = obs_data_get_bool(settings, "from_file");

	obs_property_set_visible(obs_properties_get(props, "shader_text"), !from_file);
	obs_property_set_visible(obs_properties_get(props, "shader_file_name"), from_file);

	if (from_file != filter->last_from_file) {
		filter->reload_effect = true;
	}
	filter->last_from_file = from_file;

	return true;
}

static bool shader_filter_text_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);
	if (!filter)
		return false;

	const char *shader_text = obs_data_get_string(settings, "shader_text");
	bool can_convert = strstr(shader_text, "void mainImage( out vec4") || strstr(shader_text, "void mainImage(out vec4") ||
			   strstr(shader_text, "void main()") || strstr(shader_text, "vec4 effect(vec4");
	obs_property_t *shader_convert = obs_properties_get(props, "shader_convert");
	bool visible = obs_property_visible(obs_properties_get(props, "shader_text"));
	if (obs_property_visible(shader_convert) != (can_convert && visible)) {
		obs_property_set_visible(shader_convert, can_convert && visible);
		return true;
	}
	return false;
}

static bool shader_filter_file_name_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	struct shader_filter_data *filter = obs_properties_get_param(props);
	const char *new_file_name = obs_data_get_string(settings, obs_property_name(p));

	if ((dstr_is_empty(&filter->last_path) && strlen(new_file_name)) ||
	    (filter->last_path.array && dstr_cmp(&filter->last_path, new_file_name) != 0)) {
		filter->reload_effect = true;
		dstr_copy(&filter->last_path, new_file_name);
		size_t l = strlen(new_file_name);
		if (l > 7 && strncmp(new_file_name + l - 7, ".effect", 7) == 0) {
			obs_data_set_bool(settings, "override_entire_effect", true);
		} else if (l > 7 && strncmp(new_file_name + l - 7, ".shader", 7) == 0) {
			obs_data_set_bool(settings, "override_entire_effect", false);
		}
	}

	return false;
}

static bool use_shader_elapsed_time_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);

	bool use_shader_elapsed_time = obs_data_get_bool(settings, "use_shader_elapsed_time");
	if (use_shader_elapsed_time != filter->use_shader_elapsed_time) {
		filter->reload_effect = true;
	}
	filter->use_shader_elapsed_time = use_shader_elapsed_time;

	return false;
}
static bool shader_filter_reload_effect_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct shader_filter_data *filter = data;

	filter->reload_effect = true;

	obs_source_update(filter->context, NULL);

	return false;
}

static bool add_source_to_list(void *data, obs_source_t *source)
{
	obs_property_t *p = data;
	const char *name = obs_source_get_name(source);
	size_t count = obs_property_list_item_count(p);
	size_t idx = 0;
	while (idx < count && strcmp(name, obs_property_list_item_string(p, idx)) > 0)
		idx++;
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

static bool is_var_char(char ch)
{
	return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static void convert_if_defined(struct dstr *effect_text)
{
	char *pos = strstr(effect_text->array, "#if defined(");
	while (pos) {
		size_t diff = pos - effect_text->array;
		char *ch = strstr(effect_text->array + diff + 12, ")\n");
		pos = strstr(effect_text->array + diff + 12, "#if defined(");
		if (ch && (!pos || ch < pos)) {
			*ch = ' ';
			dstr_remove(effect_text, diff, 12);
			dstr_insert(effect_text, diff, "#ifdef ");
			pos = strstr(effect_text->array + diff + 5, "#if defined(");
		}
	}
}

static void convert_atan(struct dstr *effect_text)
{
	char *pos = strstr(effect_text->array, "atan(");
	while (pos) {
		if (is_var_char(*(pos - 1))) {
			pos = strstr(pos + 5, "atan(");
			continue;
		}
		size_t diff = pos - effect_text->array;
		char *comma = strstr(pos + 5, ",");
		char *divide = strstr(pos + 5, "/");
		if (!comma && !divide)
			return;

		int depth = 0;
		char *open = strstr(pos + 5, "(");
		char *close = strstr(pos + 5, ")");
		if (!close)
			return;
		do {
			while (open && open < close) {
				depth++;
				open = strstr(open + 1, "(");
			}
			while (depth > 0 && close && (!open || close < open)) {
				depth--;
				if (depth == 0 && (!comma || comma < close))
					comma = strstr(close + 1, ",");
				if (depth == 0 && (!divide || divide < close))
					divide = strstr(close + 1, "/");
				if (comma && open && comma > open && comma < close)
					comma = NULL;
				if (divide && open && divide > open && divide < close)
					divide = NULL;

				open = strstr(close + 1, "(");
				close = strstr(close + 1, ")");
			}
		} while (depth > 0 && close);

		if (close && comma && comma < close && (!open || comma < open)) {
			//*comma = '/';
			dstr_insert(effect_text, diff + 4, "2");
		}
		if (close && divide && divide < close && (!open || divide < open)) {
			*divide = ',';
			dstr_insert(effect_text, diff + 4, "2");
		}

		pos = strstr(effect_text->array + diff + 5, "atan(");
	}
}

static void insert_begin_of_block(struct dstr *effect_text, size_t diff, char *insert)
{
	int depth = 0;
	char *ch = effect_text->array + diff;
	while (ch > effect_text->array && (*ch == ' ' || *ch == '\t'))
		ch--;
	while (ch > effect_text->array) {
		while (*ch != '=' && *ch != '(' && *ch != ')' && *ch != '+' && *ch != '-' && *ch != '/' && *ch != '*' &&
		       *ch != ' ' && *ch != '\t' && ch > effect_text->array)
			ch--;
		if (*ch == '(') {
			if (depth == 0) {
				dstr_insert(effect_text, ch - effect_text->array + 1, insert);
				return;
			}
			ch--;
			depth--;
		} else if (*ch == ')') {
			ch--;
			depth++;
		} else if (*ch == '=') {
			dstr_insert(effect_text, ch - effect_text->array + 1, insert);
			return;
		} else if (*ch == '+' || *ch == '-' || *ch == '/' || *ch == '*' || *ch == ' ' || *ch == '\t') {
			if (depth == 0) {
				dstr_insert(effect_text, ch - effect_text->array + 1, insert);
				return;
			}
			ch--;
		}
	}
}

static void insert_end_of_block(struct dstr *effect_text, size_t diff, char *insert)
{
	int depth = 0;
	char *ch = effect_text->array + diff;
	while (*ch == ' ' || *ch == '\t')
		ch++;
	while (*ch != 0) {
		while (*ch != ';' && *ch != '(' && *ch != ')' && *ch != '+' && *ch != '-' && *ch != '/' && *ch != '*' &&
		       *ch != ' ' && *ch != '\t' && *ch != ',' && *ch != 0)
			ch++;
		if (*ch == '(') {
			ch++;
			depth++;
		} else if (*ch == ')') {
			if (depth == 0) {
				dstr_insert(effect_text, ch - effect_text->array, insert);
				return;
			}
			ch++;
			depth--;
		} else if (*ch == ';') {
			dstr_insert(effect_text, ch - effect_text->array, insert);
			return;
		} else if (*ch == '+' || *ch == '-' || *ch == '/' || *ch == '*' || *ch == ' ' || *ch == '\t' || *ch == ',') {
			if (depth == 0) {
				dstr_insert(effect_text, ch - effect_text->array, insert);
				return;
			}
			ch++;
		}
	}
}

static void convert_mat_mul_var(struct dstr *effect_text, struct dstr *var_function_name)
{
	char *pos = strstr(effect_text->array, var_function_name->array);
	while (pos) {
		if (is_var_char(*(pos - 1)) || is_var_char(*(pos + var_function_name->len))) {
			pos = strstr(pos + var_function_name->len, var_function_name->array);
			continue;
		}
		size_t diff = pos - effect_text->array;
		char *ch = pos + var_function_name->len;
		while (*ch == ' ' || *ch == '\t')
			ch++;
		if (*ch == '*' && *(ch + 1) != '=') {
			size_t diff3 = ch - effect_text->array + 1;
			*ch = ',';
			insert_end_of_block(effect_text, diff3, ")");
			dstr_insert(effect_text, diff, "mul(");
			pos = strstr(effect_text->array + diff + 4 + var_function_name->len, var_function_name->array);
			continue;
		}
		ch = pos - 1;
		while ((*ch == ' ' || *ch == '\t') && ch > effect_text->array)
			ch--;
		if (*ch == '=' && *(ch - 1) == '*') {
			char *end = ch - 2;
			while ((*end == ' ' || *end == '\t') && end > effect_text->array)
				end--;
			char *start = end;
			while ((is_var_char(*start) || *start == '.') && start > effect_text->array)
				start--;
			start++;
			end++;

			diff = ch - 1 - effect_text->array;

			struct dstr insert = {0};
			dstr_init_copy(&insert, "= mul(");
			dstr_ncat(&insert, start, end - start);
			dstr_cat(&insert, ",");
			dstr_remove(effect_text, diff, 2);
			dstr_insert(effect_text, diff, insert.array);

			char *line_end = effect_text->array + diff;
			while (*line_end != ';' && *line_end != 0)
				line_end++;

			dstr_insert(effect_text, line_end - effect_text->array, ")");

			pos = strstr(effect_text->array + diff + insert.len + 1 + var_function_name->len, var_function_name->array);
			dstr_free(&insert);

			continue;
		} else if (*ch == '*') {
			size_t diff2 = ch - effect_text->array - 1;
			*ch = ',';
			insert_end_of_block(effect_text, diff + var_function_name->len, ")");
			insert_begin_of_block(effect_text, diff2, "mul(");

			pos = strstr(effect_text->array + diff + 4 + var_function_name->len, var_function_name->array);
			continue;
		}

		pos = strstr(effect_text->array + diff + var_function_name->len, var_function_name->array);
	}
}

static void convert_mat_mul(struct dstr *effect_text, char *var_type)
{
	size_t len = strlen(var_type);
	char *pos = strstr(effect_text->array, var_type);
	while (pos) {
		if (is_var_char(*(pos - 1))) {
			pos = strstr(pos + len, var_type);
			continue;
		}
		size_t diff = pos - effect_text->array;
		char *begin = pos + len;
		if (*begin == '(') {
			char *ch = pos - 1;
			while ((*ch == ' ' || *ch == '\t') && ch > effect_text->array)
				ch--;
			if (*ch == '=' && *(ch - 1) == '*') {
				// mat constructor with *= in front of it
				char *end = ch - 2;
				while ((*end == ' ' || *end == '\t') && end > effect_text->array)
					end--;
				char *start = end;
				while ((is_var_char(*start) || *start == '.') && start > effect_text->array)
					start--;
				start++;
				end++;

				size_t diff2 = ch - effect_text->array - 1;

				struct dstr insert = {0};
				dstr_init_copy(&insert, "= mul(");
				dstr_ncat(&insert, start, end - start);
				dstr_cat(&insert, ",");
				dstr_remove(effect_text, diff2, 2);
				dstr_insert(effect_text, diff2, insert.array);

				char *line_end = effect_text->array + diff2;
				while (*line_end != ';' && *line_end != 0)
					line_end++;

				dstr_insert(effect_text, line_end - effect_text->array, ")");

				pos = strstr(effect_text->array + diff + insert.len + len + 1, var_type);
				dstr_free(&insert);

				continue;
			} else if (*ch == '*') {
				// mat constructor with * in front of it
				size_t diff2 = ch - effect_text->array - 1;
				*ch = ',';
				insert_end_of_block(effect_text, diff + len, ")");
				insert_begin_of_block(effect_text, diff2, "mul(");

				pos = strstr(effect_text->array + diff + len + 4, var_type);
				continue;
			}

			int depth = 1;
			ch = begin + 1;
			while (*ch != 0) {
				while (*ch != ';' && *ch != '(' && *ch != ')' && *ch != '+' && *ch != '-' && *ch != '/' &&
				       *ch != '*' && *ch != 0)
					ch++;
				if (*ch == '(') {
					ch++;
					depth++;
				} else if (*ch == ')') {
					if (depth == 0) {
						break;
					}
					ch++;
					depth--;
				} else if (*ch == '*') {
					if (depth == 0) {
						//mat constructor follow by *
						*ch = ',';
						insert_end_of_block(effect_text, ch - effect_text->array + 1, ")");
						dstr_insert(effect_text, diff, "mul(");
						break;
					}
					ch++;
				} else if (*ch == ';') {
					break;
				} else if (depth == 0) {
					break;
				} else if (*ch != 0) {
					ch++;
				}
			}
		}
		if (*begin != ' ' && *begin != '\t') {
			pos = strstr(pos + len, var_type);
			continue;
		}
		while (*begin == ' ' || *begin == '\t')
			begin++;
		if (!is_var_char(*begin)) {
			pos = strstr(pos + len, var_type);
			continue;
		}
		char *end = begin;
		while (is_var_char(*end))
			end++;
		struct dstr var_function_name = {0};
		dstr_ncat(&var_function_name, begin, end - begin);

		convert_mat_mul_var(effect_text, &var_function_name);
		dstr_free(&var_function_name);

		pos = strstr(effect_text->array + diff + len, var_type);
	}
}

static bool is_in_function(struct dstr *effect_text, size_t diff)
{
	char *pos = effect_text->array + diff;
	int depth = 0;
	while (depth >= 0) {
		char *end = strstr(pos, "}");
		char *begin = strstr(pos, "{");
		if (end && begin && begin < end) {
			pos = begin + 1;
			depth++;
		} else if (end && begin && end < begin) {
			pos = end + 1;
			depth--;
		} else if (end) {
			pos = end + 1;
			depth--;
		} else if (begin && depth < 0) {
			break;
		} else if (begin) {
			pos = begin + 1;
			depth++;
		} else {
			break;
		}
	}
	return depth != 0;
}

static void convert_init(struct dstr *effect_text, char *name)
{
	const size_t len = strlen(name);
	char extra = 0;
	char *pos = strstr(effect_text->array, name);
	while (pos) {
		size_t diff = pos - effect_text->array;
		if (pos > effect_text->array && is_var_char(*(pos - 1))) {
			pos = strstr(effect_text->array + diff + len, name);
			continue;
		}
		char *ch = pos + len;
		if (*ch >= '2' && *ch <= '9') {
			extra = *ch;
			ch++;
		} else {
			extra = 0;
		}
		if (*ch != ' ' && *ch != '\t') {
			pos = strstr(effect_text->array + diff + len, name);
			continue;
		}
		char *begin = pos - 1;
		while (begin > effect_text->array && (*begin == ' ' || *begin == '\t'))
			begin--;
		if (*begin == '(' || *begin == ',' ||
		    (is_var_char(*begin) && memcmp("uniform", begin - 6, 7) != 0 && memcmp("const", begin - 4, 5) != 0)) {
			pos = strstr(effect_text->array + diff + len, name);
			continue;
		}
		if (!is_in_function(effect_text, diff)) {
			while (true) {
				while (*ch != 0 && (*ch == ' ' || *ch == '\t'))
					ch++;
				while (is_var_char(*ch))
					ch++;
				while (*ch != 0 && (*ch == ' ' || *ch == '\t'))
					ch++;
				if (*ch != ',')
					break;
				*ch = ';';
				diff = ch - effect_text->array;
				dstr_insert(effect_text, diff + 1, " ");
				if (extra) {
					dstr_insert_ch(effect_text, diff + 1, extra);
				}
				dstr_insert(effect_text, diff + 1, name);
				if (memcmp("const", begin - 4, 5) == 0) {
					dstr_insert(effect_text, diff + 1, "\nconst ");
					diff += 9;
				} else {
					dstr_insert(effect_text, diff + 1, "\nuniform ");
					diff += 11;
				}
				if (extra)
					diff++;
				ch = effect_text->array + diff + len;
			}

			if ((*ch == '=' && *(ch + 1) != '=') || *ch == ';') {
				if (memcmp("uniform", begin - 6, 7) != 0 && memcmp("const", begin - 4, 5) != 0) {
					dstr_insert(effect_text, begin - effect_text->array + 1, "uniform ");
					diff += 8;
				}
			}
		}
		pos = strstr(effect_text->array + diff + len, name);
	}
}

static void convert_vector_init(struct dstr *effect_text, char *name, int count)
{
	const size_t len = strlen(name);

	char *pos = strstr(effect_text->array, name);
	while (pos) {
		size_t diff = pos - effect_text->array;
		char *ch = pos + len;
		int depth = 0;
		int function_depth = -1;
		bool only_one = true;
		bool only_numbers = true;
		bool only_float = true;
		while (*ch != 0 && (only_numbers || only_float)) {
			if (*ch == '(') {
				depth++;
			} else if (*ch == ')') {
				if (depth == 0) {
					break;
				}
				depth--;
				if (depth == function_depth) {
					function_depth = -1;
				}
			} else if (*ch == ',') {
				if (depth == 0) {
					only_one = false;
				}
			} else if (*ch == ';') {
				only_one = false;
				break;
			} else if (is_var_char(*ch) && (*ch < '0' || *ch > '9')) {
				only_numbers = false;
				char *begin = ch;
				while (is_var_char(*ch))
					ch++;
				if (*ch == '.') {
					size_t c = 1;
					while (is_var_char(*(ch + c)))
						c++;
					if (c != 2 && function_depth < 0)
						only_float = false;
					ch += c - 1;
				} else if (function_depth >= 0) {
					ch--;
				} else if (*ch == '(' &&
					   (strncmp(begin, "length(", 7) == 0 || strncmp(begin, "float(", 6) == 0 ||
					    strncmp(begin, "uint(", 5) == 0 || strncmp(begin, "int(", 4) == 0 ||
					    strncmp(begin, "asfloat(", 8) == 0 || strncmp(begin, "asdouble(", 9) == 0 ||
					    strncmp(begin, "asint(", 6) == 0 || strncmp(begin, "asuint(", 7) == 0 ||
					    strncmp(begin, "determinant(", 12) == 0 || strncmp(begin, "distance(", 9) == 0 ||
					    strncmp(begin, "dot(", 4) == 0 || strncmp(begin, "countbits(", 10) == 0 ||
					    strncmp(begin, "firstbithigh(", 13) == 0 || strncmp(begin, "firstbitlow(", 12) == 0 ||
					    strncmp(begin, "reversebits(", 12) == 0)) {
					function_depth = depth;
					depth++;
				} else if (*ch == '(' &&
					   (strncmp(begin, "abs(", 4) == 0 || strncmp(begin, "acos(", 5) == 0 ||
					    strncmp(begin, "asin(", 5) == 0 || strncmp(begin, "atan(", 5) == 0 ||
					    strncmp(begin, "atan2(", 6) == 0 || strncmp(begin, "ceil(", 5) == 0 ||
					    strncmp(begin, "clamp(", 6) == 0 || strncmp(begin, "cos(", 4) == 0 ||
					    strncmp(begin, "cosh(", 5) == 0 || strncmp(begin, "ddx(", 4) == 0 ||
					    strncmp(begin, "ddy(", 4) == 0 || strncmp(begin, "degrees(", 8) == 0 ||
					    strncmp(begin, "exp(", 4) == 0 || strncmp(begin, "exp2(", 5) == 0 ||
					    strncmp(begin, "floor(", 6) == 0 || strncmp(begin, "fma(", 4) == 0 ||
					    strncmp(begin, "fmod(", 5) == 0 || strncmp(begin, "frac(", 5) == 0 ||
					    strncmp(begin, "frexp(", 6) == 0 || strncmp(begin, "fwidth(", 7) == 0 ||
					    strncmp(begin, "ldexp(", 6) == 0 || strncmp(begin, "lerp(", 5) == 0 ||
					    strncmp(begin, "log(", 4) == 0 || strncmp(begin, "log10(", 6) == 0 ||
					    strncmp(begin, "log2(", 5) == 0 || strncmp(begin, "mad(", 4) == 0 ||
					    strncmp(begin, "max(", 4) == 0 || strncmp(begin, "min(", 4) == 0 ||
					    strncmp(begin, "modf(", 5) == 0 || strncmp(begin, "mod(", 4) == 0 ||
					    strncmp(begin, "mul(", 4) == 0 || strncmp(begin, "normalize(", 10) == 0 ||
					    strncmp(begin, "pow(", 4) == 0 || strncmp(begin, "radians(", 8) == 0 ||
					    strncmp(begin, "rcp(", 4) == 0 || strncmp(begin, "reflect(", 8) == 0 ||
					    strncmp(begin, "refract(", 8) == 0 || strncmp(begin, "round(", 6) == 0 ||
					    strncmp(begin, "rsqrt(", 6) == 0 || strncmp(begin, "saturate(", 9) == 0 ||
					    strncmp(begin, "sign(", 5) == 0 || strncmp(begin, "sin(", 4) == 0 ||
					    strncmp(begin, "sincos(", 7) == 0 || strncmp(begin, "sinh(", 5) == 0 ||
					    strncmp(begin, "smoothstep(", 11) == 0 || strncmp(begin, "sqrt(", 5) == 0 ||
					    strncmp(begin, "step(", 5) == 0 || strncmp(begin, "tan(", 4) == 0 ||
					    strncmp(begin, "tanh(", 5) == 0 || strncmp(begin, "transpose(", 10) == 0 ||
					    strncmp(begin, "trunc(", 6) == 0)) {
					depth++;
				} else {
					struct dstr find = {0};
					bool found = false;
					dstr_copy(&find, "float ");
					dstr_ncat(&find, begin, ch - begin + (*ch == '(' ? 1 : 0));
					char *t = strstr(effect_text->array, find.array);
					while (t != NULL) {
						t += find.len;
						if (*ch == '(') {
							found = true;
							break;
						} else if (!is_var_char(*t)) {
							found = true;
							break;
						}
						t = strstr(t, find.array);
					}
					if (!found) {
						dstr_copy(&find, "int ");
						dstr_ncat(&find, begin, ch - begin + (*ch == '(' ? 1 : 0));
						t = strstr(effect_text->array, find.array);
						while (t != NULL) {
							t += find.len;
							if (*ch == '(') {
								found = true;
								break;
							} else if (!is_var_char(*t)) {
								found = true;
								break;
							}
							t = strstr(t, find.array);
						}
					}
					if (!found && *ch != '(') {
						dstr_copy(&find, "#define ");
						dstr_ncat(&find, begin, ch - begin);
						char *t = strstr(effect_text->array, find.array);
						while (t != NULL) {
							t += find.len;
							if (!is_var_char(*t)) {
								while (*t == ' ' || *t == '\t')
									t++;
								if (*t >= '0' && *t <= '9') {
									found = true;
									break;
								}
							}
							t = strstr(t, find.array);
						}
					}
					if (!found) {
						only_float = false;
					} else if (*ch == '(') {
						function_depth = depth;
					}
					dstr_free(&find);
					ch--;
				}
			} else if ((*ch < '0' || *ch > '9') && *ch != '.' && *ch != ' ' && *ch != '\t' && *ch != '+' &&
				   *ch != '-' && *ch != '*' && *ch != '/') {
				only_numbers = false;
			}
			ch++;
		}
		size_t end_diff = ch - effect_text->array;
		if (count > 1 && only_one && (only_numbers || only_float)) {
			//only 1 simple arg in the float4
			struct dstr found = {0};
			dstr_init(&found);
			dstr_ncat(&found, pos, ch - pos + 1);

			struct dstr replacement = {0};
			dstr_init_copy(&replacement, name);
			dstr_ncat(&replacement, pos + len, ch - (pos + len));
			for (int i = 1; i < count; i++) {
				dstr_cat(&replacement, ",");
				dstr_ncat(&replacement, pos + len, ch - (pos + len));
			}
			dstr_cat(&replacement, ")");

			dstr_replace(effect_text, found.array, replacement.array);

			end_diff -= found.len;
			end_diff += replacement.len;
			dstr_free(&replacement);
			dstr_free(&found);
		}

		if (!is_in_function(effect_text, diff)) {
			char *begin = effect_text->array + diff - 1;
			while (begin > effect_text->array && (*begin == ' ' || *begin == '\t'))
				begin--;
			if (*begin == '=') {
				begin--;
				while (begin > effect_text->array && (*begin == ' ' || *begin == '\t'))
					begin--;
				while (is_var_char(*begin))
					begin--;
				while (begin > effect_text->array && (*begin == ' ' || *begin == '\t'))
					begin--;
				if (memcmp(name, begin - len + 2, len - 1) == 0) {

					begin -= len - 1;
					while (begin > effect_text->array && (*begin == ' ' || *begin == '\t'))
						begin--;
					if (memcmp("uniform", begin - 6, 7) != 0 && memcmp("const", begin - 4, 5) != 0) {
						dstr_insert(effect_text, begin - effect_text->array + 1, "uniform ");
						diff += 8;
						end_diff += 8;
					}
					if (effect_text->array[end_diff] == ')') {
						if (count > 1) {
							effect_text->array[end_diff] = '}';
							dstr_remove(effect_text, diff, len);
							dstr_insert(effect_text, diff, "{");
						} else {
							dstr_remove(effect_text, end_diff, 1);
							dstr_remove(effect_text, diff, len);
						}
					}
				}
			}
		}

		pos = strstr(effect_text->array + diff + len, name);
	}
}

static void convert_if0(struct dstr *effect_text)
{
	char *begin = strstr(effect_text->array, "#if 0");
	while (begin) {
		size_t diff = begin - effect_text->array;
		char *end = strstr(begin, "#endif");
		if (!end)
			return;
		char *el = strstr(begin, "#else");
		char *eli = strstr(begin, "#elif");
		if (eli && eli < end && (!el || eli < el)) {
			//replace #elif with #if
			dstr_remove(effect_text, diff, el - begin + 5);
			dstr_insert(effect_text, diff, "#if");
			begin = strstr(effect_text->array + diff + 3, "#if 0");
		} else if (el && el < end) {
			dstr_remove(effect_text, end - effect_text->array,
				    6); // #endif
			dstr_remove(effect_text, diff, el - begin + 5);
			begin = strstr(effect_text->array + diff, "#if 0");
		} else if (!el || el > end) {
			dstr_remove(effect_text, diff, end - begin + 6);
			begin = strstr(effect_text->array + diff, "#if 0");
		} else {
			begin = strstr(effect_text->array + diff + 5, "#if 0");
		}
	}
}

static void convert_if1(struct dstr *effect_text)
{
	char *begin = strstr(effect_text->array, "#if 1");
	while (begin) {
		size_t diff = begin - effect_text->array;
		char *end = strstr(begin, "#endif");
		if (!end)
			return;
		char *el = strstr(begin, "#el");
		if (el && el < end) {
			dstr_remove(effect_text, el - effect_text->array,
				    end - el + 6); // #endif
			end = strstr(effect_text->array + diff, "\n");
			if (end)
				dstr_remove(effect_text, diff, end - (effect_text->array + diff));
			begin = strstr(effect_text->array + diff, "#if 1");
		} else if (!el || el > end) {
			dstr_remove(effect_text, end - effect_text->array, 6);
			end = strstr(effect_text->array + diff, "\n");
			if (end)
				dstr_remove(effect_text, diff, end - (effect_text->array + diff));
			begin = strstr(effect_text->array + diff, "#if 1");
		} else {
			begin = strstr(effect_text->array + diff + 5, "#if 1");
		}
	}
}

static void convert_define(struct dstr *effect_text)
{
	char *pos = strstr(effect_text->array, "#define ");
	while (pos) {
		size_t diff = pos - effect_text->array;
		char *start = pos + 8;
		while (*start == ' ' || *start == '\t')
			start++;
		char *end = start;
		while (*end != ' ' && *end != '\t' && *end != '\n' && *end != 0)
			end++;
		char *t = strstr(start, "(");
		if (t && t < end) {
			// don't replace macro
			pos = strstr(effect_text->array + diff + 8, "#define ");
			continue;
		}

		struct dstr def_name = {0};
		dstr_ncat(&def_name, start, end - start);

		start = end;
		while (*start == ' ' || *start == '\t')
			start++;

		end = start;
		while (*end != '\n' && *end != 0 && (*end != '/' || *(end + 1) != '/'))
			end++;

		t = strstr(start, "(");
		if (*start == '(' || (t && t < end)) {
			struct dstr replacement = {0};
			dstr_ncat(&replacement, start, end - start);

			dstr_remove(effect_text, diff, end - (effect_text->array + diff));

			dstr_replace(effect_text, def_name.array, replacement.array);

			dstr_free(&replacement);
			pos = strstr(effect_text->array + diff, "#define ");
		} else {
			pos = strstr(effect_text->array + diff + 8, "#define ");
		}
		dstr_free(&def_name);
	}
}

static void convert_return(struct dstr *effect_text, struct dstr *var_name, size_t main_diff)
{
	size_t count = 0;
	char *pos = strstr(effect_text->array + main_diff, var_name->array);
	while (pos) {
		if (is_var_char(*(pos - 1)) || (*(pos - 1) == '/' && *(pos - 2) == '/')) {
			pos = strstr(pos + var_name->len, var_name->array);
			continue;
		}
		size_t diff = pos - effect_text->array;
		char *ch = pos + var_name->len;
		if (*ch == '.') {
			ch++;
			while (is_var_char(*ch))
				ch++;
		}
		while (*ch == ' ' || *ch == '\t')
			ch++;

		if (*ch == '=' || (*(ch + 1) == '=' && (*ch == '*' || *ch == '/' || *ch == '+' || *ch == '-'))) {
			count++;
		}

		pos = strstr(effect_text->array + diff + var_name->len, var_name->array);
	}
	if (count == 0)
		return;
	if (count == 1) {
		pos = strstr(effect_text->array + main_diff, var_name->array);
		while (pos) {
			if (is_var_char(*(pos - 1)) || (*(pos - 1) == '/' && *(pos - 2) == '/')) {
				pos = strstr(pos + var_name->len, var_name->array);
				continue;
			}
			size_t diff = pos - effect_text->array;
			char *ch = pos + var_name->len;
			if (*ch == '.') {
				ch++;
				while (is_var_char(*ch))
					ch++;
			}

			while (*ch == ' ' || *ch == '\t')
				ch++;

			if (*ch == '=') {
				dstr_remove(effect_text, diff, ch - pos + 1);
				dstr_insert(effect_text, diff, "return ");
				return;
			} else if (*(ch + 1) == '=' && (*ch == '*' || *ch == '/' || *ch == '+' || *ch == '-')) {
				dstr_remove(effect_text, diff, ch - pos + 2);
				dstr_insert(effect_text, diff, "return ");
				return;
			}

			pos = strstr(effect_text->array + diff + var_name->len, var_name->array);
		}
		return;
	}

	size_t replaced = 0;
	size_t start_diff = 0;
	bool declared = false;
	pos = strstr(effect_text->array + main_diff, "{");
	if (pos) {
		size_t insert_diff = pos - effect_text->array + 1;
		dstr_insert(effect_text, insert_diff, " = float4(0.0,0.0,0.0,1.0);\n");
		dstr_insert(effect_text, insert_diff, var_name->array);
		dstr_insert(effect_text, insert_diff, "\n\tfloat4 ");
		declared = true;
		start_diff = insert_diff - main_diff + 37 + var_name->len;
	}

	pos = strstr(effect_text->array + main_diff + start_diff, var_name->array);
	while (pos) {
		size_t diff = pos - effect_text->array;
		char *ch = pos + var_name->len;
		bool part = false;
		if (*ch == '.') {
			part = true;
			ch++;
			while (is_var_char(*ch))
				ch++;
		}
		while (*ch == ' ' || *ch == '\t')
			ch++;

		if (*ch == '=') {
			replaced++;
			if (replaced == 1 && !declared) {
				if (part) {
					dstr_insert(effect_text, diff, " = float4(0.0,0.0,0.0,1.0);\n");
					dstr_insert(effect_text, diff, var_name->array);
					dstr_insert(effect_text, diff, "float4 ");
					diff += 35 + var_name->len;
				} else {
					dstr_insert(effect_text, diff, "float4 ");
					diff += 7;
				}
			} else if (replaced == count) {
				if (part) {
					while (*ch != ';' && *ch != 0)
						ch++;
					diff = ch - effect_text->array + 1;
					dstr_insert(effect_text, diff, ";");
					dstr_insert(effect_text, diff, var_name->array);
					dstr_insert(effect_text, diff, "\n\treturn ");
				} else {
					dstr_remove(effect_text, diff, ch - pos + 1);
					dstr_insert(effect_text, diff, "return ");
				}
				return;
			}
		} else if (*(ch + 1) == '=' && (*ch == '*' || *ch == '/' || *ch == '+' || *ch == '-')) {
			replaced++;
			if (replaced == 1 && !declared) {
				dstr_insert(effect_text, diff, " = float4(0.0,0.0,0.0,1.0);\n");
				dstr_insert(effect_text, diff, var_name->array);
				dstr_insert(effect_text, diff, "float4 ");
				diff += 35 + var_name->len;
			} else if (replaced == count) {
				while (*ch != ';' && *ch != 0)
					ch++;
				diff = ch - effect_text->array + 1;
				dstr_insert(effect_text, diff, ";");
				dstr_insert(effect_text, diff, var_name->array);
				dstr_insert(effect_text, diff, "\n\treturn ");
				return;
			}
		}

		pos = strstr(effect_text->array + diff + var_name->len, var_name->array);
	}
}

static bool shader_filter_convert(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	if (!data)
		return false;
	struct shader_filter_data *filter = data;
	obs_data_t *settings = obs_source_get_settings(filter->context);
	if (!settings)
		return false;
	struct dstr effect_text = {0};
	dstr_init_copy(&effect_text, obs_data_get_string(settings, "shader_text"));

	//convert_define(&effect_text);

	size_t start_diff = 24;
	bool main_no_args = false;
	int uv = 0;
	char *main_pos = strstr(effect_text.array, "void mainImage(out vec4");
	if (!main_pos) {
		main_pos = strstr(effect_text.array, "void mainImage( out vec4");
		start_diff++;
	}
	if (!main_pos) {
		main_pos = strstr(effect_text.array, "void main()");
		if (main_pos)
			main_no_args = true;
	}
	if (!main_pos) {
		main_pos = strstr(effect_text.array, "vec4 effect(vec4");
		start_diff = 17;
		uv = 1;
	}

	if (!main_pos) {
		dstr_free(&effect_text);
		obs_data_release(settings);
		return false;
	}
	size_t main_diff = main_pos - effect_text.array;
	struct dstr return_color_name = {0};
	struct dstr coord_name = {0};
	if (main_no_args) {

		dstr_replace(&effect_text, "void main()", "float4 mainImage(VertData v_in) : TARGET");
		if (strstr(effect_text.array, "varying vec2 position;")) {
			uv = 1;
			dstr_init_copy(&coord_name, "position");
		} else if (strstr(effect_text.array, "varying vec2 pos;")) {
			uv = 1;
			dstr_init_copy(&coord_name, "pos");
		} else if (strstr(effect_text.array, "fNormal")) {
			uv = 2;
			dstr_init_copy(&coord_name, "fNormal");
		} else {
			uv = 0;
			dstr_init_copy(&coord_name, "gl_FragCoord");
		}

		char *out_start = strstr(effect_text.array, "out vec4");
		if (out_start) {
			char *start = out_start + 9;
			while (*start == ' ' || *start == '\t')
				start++;
			char *end = start;
			while (*end != ' ' && *end != '\t' && *end != '\n' && *end != ',' && *end != '(' && *end != ')' &&
			       *end != ';' && *end != 0)
				end++;
			dstr_ncat(&return_color_name, start, end - start);
			while (*end == ' ' || *end == '\t')
				end++;
			if (*end == ';')
				dstr_remove(&effect_text, out_start - effect_text.array, (end + 1) - out_start);
		} else {
			dstr_init_copy(&return_color_name, "gl_FragColor");
		}
	} else {

		char *start = main_pos + start_diff;
		while (*start == ' ' || *start == '\t')
			start++;
		char *end = start;
		while (*end != ' ' && *end != '\t' && *end != '\n' && *end != ',' && *end != ')' && *end != 0)
			end++;

		dstr_ncat(&return_color_name, start, end - start);

		start = strstr(end, ",");
		if (!start) {
			dstr_free(&effect_text);
			dstr_free(&return_color_name);
			obs_data_release(settings);
			return false;
		}
		start++;
		while (*start == ' ' || *start == '\t')
			start++;
		char *v2i = strstr(start, "in vec2 ");
		char *v2 = strstr(start, "vec2 ");
		char *close = strstr(start, ")");
		if (v2i && close && v2i < close) {
			start = v2i + 8;
		} else if (v2 && close && v2 < close) {
			start = v2 + 5;
		} else {
			if (*start == 'i' && *(start + 1) == 'n' && (*(start + 2) == ' ' || *(start + 2) == '\t'))
				start += 3;
			while (*start == ' ' || *start == '\t')
				start++;
			if (*start == 'v' && *(start + 1) == 'e' && *(start + 2) == 'c' && *(start + 3) == '2' &&
			    (*(start + 4) == ' ' || *(start + 4) == '\t'))
				start += 5;
		}
		while (*start == ' ' || *start == '\t')
			start++;

		end = start;
		while (*end != ' ' && *end != '\t' && *end != '\n' && *end != ',' && *end != ')' && *end != 0)
			end++;

		dstr_ncat(&coord_name, start, end - start);

		while (*end != ')' && *end != 0)
			end++;
		size_t idx = main_pos - effect_text.array;
		dstr_remove(&effect_text, idx, end - main_pos + 1);
		dstr_insert(&effect_text, idx, "float4 mainImage(VertData v_in) : TARGET");
	}

	convert_return(&effect_text, &return_color_name, main_diff);

	dstr_free(&return_color_name);

	if (dstr_cmp(&coord_name, "fragCoord") != 0) {
		if (dstr_find(&coord_name, "fragCoord")) {
			dstr_replace(&effect_text, coord_name.array, "fragCoord");
		} else {
			char *pos = strstr(effect_text.array, coord_name.array);
			while (pos) {
				size_t diff = pos - effect_text.array;
				if (((main_no_args || diff < main_diff) || diff > main_diff + 24) && !is_var_char(*(pos - 1)) &&
				    !is_var_char(*(pos + coord_name.len))) {
					dstr_remove(&effect_text, diff, coord_name.len);
					dstr_insert(&effect_text, diff, "fragCoord");
					pos = strstr(effect_text.array + diff + 9, coord_name.array);

				} else {
					pos = strstr(effect_text.array + diff + coord_name.len, coord_name.array);
				}
			}
		}
	}
	dstr_free(&coord_name);

	convert_if0(&effect_text);
	convert_if1(&effect_text);

	dstr_replace(&effect_text, "varying vec3", "//varying vec3");
	dstr_replace(&effect_text, "precision highp float;", "//precision highp float;");
	if (uv == 1) {
		dstr_replace(&effect_text, "fragCoord", "v_in.uv");
	} else if (uv == 2) {
		dstr_replace(&effect_text, "fragCoord.xy", "v_in.uv");
		dstr_replace(&effect_text, "fragCoord", "float3(v_in.uv,0.0)");
	} else {
		dstr_replace(&effect_text, "fragCoord.xy/iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "fragCoord.xy / iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "fragCoord/iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "fragCoord / iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "fragCoord", "(v_in.uv * uv_size)");
		dstr_replace(&effect_text, "gl_FragCoord.xy/iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "gl_FragCoord.xy / iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "gl_FragCoord/iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "gl_FragCoord / iResolution.xy", "v_in.uv");
		dstr_replace(&effect_text, "gl_FragCoord", "(v_in.uv * uv_size)");
	}
	dstr_replace(&effect_text, "love_ScreenSize", "uv_size");
	dstr_replace(&effect_text, "u_resolution", "uv_size");
	dstr_replace(&effect_text, "uResolution", "uv_size");
	dstr_replace(&effect_text, "iResolution.xyz", "float3(uv_size,0.0)");
	dstr_replace(&effect_text, "iResolution.xy", "uv_size");
	dstr_replace(&effect_text, "iResolution.x", "uv_size.x");
	dstr_replace(&effect_text, "iResolution.y", "uv_size.y");
	dstr_replace(&effect_text, "iResolution", "float4(uv_size,uv_pixel_interval)");

	dstr_replace(&effect_text, "uniform vec2 uv_size;", "");

	if (strstr(effect_text.array, "iTime"))
		dstr_replace(&effect_text, "iTime", "elapsed_time");
	else if (strstr(effect_text.array, "uTime"))
		dstr_replace(&effect_text, "uTime", "elapsed_time");
	else if (strstr(effect_text.array, "u_time"))
		dstr_replace(&effect_text, "u_time", "elapsed_time");
	else
		dstr_replace(&effect_text, "time", "elapsed_time");

	dstr_replace(&effect_text, "uniform float elapsed_time;", "");

	dstr_replace(&effect_text, "bvec2", "bool2");
	dstr_replace(&effect_text, "bvec3", "bool3");
	dstr_replace(&effect_text, "bvec4", "bool4");

	dstr_replace(&effect_text, "ivec4", "int4");
	dstr_replace(&effect_text, "ivec3", "int3");
	dstr_replace(&effect_text, "ivec2", "int2");

	dstr_replace(&effect_text, "uvec4", "uint4");
	dstr_replace(&effect_text, "uvec3", "uint3");
	dstr_replace(&effect_text, "uvec2", "uint2");

	dstr_replace(&effect_text, "vec4", "float4");
	dstr_replace(&effect_text, "vec3", "float3");
	dstr_replace(&effect_text, "vec2", "float2");

	dstr_replace(&effect_text, " number ", " float ");

	//dstr_replace(&effect_text, "mat4", "float4x4");
	//dstr_replace(&effect_text, "mat3", "float3x3");
	//dstr_replace(&effect_text, "mat2", "float2x2");

	dstr_replace(&effect_text, "dFdx(", "ddx(");
	dstr_replace(&effect_text, "dFdy(", "ddy(");
	dstr_replace(&effect_text, "mix(", "lerp(");
	dstr_replace(&effect_text, "fract(", "frac(");

	dstr_replace(&effect_text, "extern bool", "uniform bool");
	dstr_replace(&effect_text, "extern uint", "uniform uint");
	dstr_replace(&effect_text, "extern int", "uniform int");
	dstr_replace(&effect_text, "extern float", "uniform float");

	convert_init(&effect_text, "bool");
	convert_init(&effect_text, "uint");
	convert_init(&effect_text, "int");
	convert_init(&effect_text, "float");

	convert_vector_init(&effect_text, "bool(", 1);
	convert_vector_init(&effect_text, "bool2(", 2);
	convert_vector_init(&effect_text, "bool3(", 3);
	convert_vector_init(&effect_text, "bool4(", 4);
	convert_vector_init(&effect_text, "uint(", 1);
	convert_vector_init(&effect_text, "uint2(", 2);
	convert_vector_init(&effect_text, "uint3(", 3);
	convert_vector_init(&effect_text, "uint4(", 4);
	convert_vector_init(&effect_text, "int(", 1);
	convert_vector_init(&effect_text, "int2(", 2);
	convert_vector_init(&effect_text, "int3(", 3);
	convert_vector_init(&effect_text, "int4(", 4);
	convert_vector_init(&effect_text, "float(", 1);
	convert_vector_init(&effect_text, "float2(", 2);
	convert_vector_init(&effect_text, "float3(", 3);
	convert_vector_init(&effect_text, "float4(", 4);
	convert_vector_init(&effect_text, "mat2(", 4);
	convert_vector_init(&effect_text, "mat3(", 9);
	convert_vector_init(&effect_text, "mat4(", 16);

	convert_atan(&effect_text);
	convert_mat_mul(&effect_text, "mat2");
	convert_mat_mul(&effect_text, "mat3");
	convert_mat_mul(&effect_text, "mat4");

	dstr_replace(&effect_text, "point(", "point2(");
	dstr_replace(&effect_text, "line(", "line2(");

	dstr_replace(&effect_text, "#version ", "//#version ");

	convert_if_defined(&effect_text);

	dstr_replace(&effect_text, "acos(-1.0)", "3.14159265359");
	dstr_replace(&effect_text, "acos(-1.)", "3.14159265359");
	dstr_replace(&effect_text, "acos(-1)", "3.14159265359");

	struct dstr insert_text = {0};
	dstr_init_copy(&insert_text, "#ifndef OPENGL\n");

	if (dstr_find(&effect_text, "mat2"))
		dstr_cat(&insert_text, "#define mat2 float2x2\n");
	if (dstr_find(&effect_text, "mat3"))
		dstr_cat(&insert_text, "#define mat3 float3x3\n");
	if (dstr_find(&effect_text, "mat4"))
		dstr_cat(&insert_text, "#define mat4 float4x4\n");

	if (dstr_find(&effect_text, "mod("))
		dstr_cat(&insert_text, "#define mod(x,y) (x - y * floor(x / y))\n");
	dstr_cat(&insert_text, "#endif\n");

	if (dstr_find(&effect_text, "iMouse") && !dstr_find(&effect_text, "float2 iMouse"))
		dstr_cat(
			&insert_text,
			"uniform float4 iMouse<\nstring widget_type = \"slider\";\nfloat minimum=0.0;\nfloat maximum=1000.0;\nfloat step=1.0;\n>;\n");

	if (dstr_find(&effect_text, "iFrame") && !dstr_find(&effect_text, "float iFrame"))
		dstr_cat(&insert_text, "uniform float iFrame;\n");

	if (dstr_find(&effect_text, "iSampleRate") && !dstr_find(&effect_text, "float iSampleRate"))
		dstr_cat(&insert_text, "uniform float iSampleRate;\n");

	if (dstr_find(&effect_text, "iTimeDelta") && !dstr_find(&effect_text, "float iTimeDelta"))
		dstr_cat(&insert_text, "uniform float iTimeDelta;\n");

	int num_textures = 0;
	struct dstr texture_name = {0};
	struct dstr replacing = {0};
	struct dstr replacement = {0};

	char *texture_find[] = {"texture(", "texture2D(", "texelFetch(", "Texel(", "textureLod("};
	char *texture = NULL;
	size_t texture_diff = 0;
	for (size_t i = 0; i < sizeof(texture_find) / sizeof(char *); i++) {
		char *t = strstr(effect_text.array, texture_find[i]);
		if (t && (!texture || t < texture)) {
			texture = t;
			texture_diff = strlen(texture_find[i]);
		}
	}
	while (texture) {
		const size_t diff = texture - effect_text.array;
		if (is_var_char(*(texture - 1))) {
			texture = NULL;
			size_t prev_diff = texture_diff;
			for (size_t i = 0; i < 3; i++) {
				char *t = strstr(effect_text.array + diff + prev_diff, texture_find[i]);
				if (t && (!texture || t < texture)) {
					texture = t;
					texture_diff = strlen(texture_find[i]);
				}
			}
			continue;
		}
		char *start = texture + texture_diff;
		while (*start == ' ' || *start == '\t')
			start++;
		char *end = start;
		while (*end != ' ' && *end != '\t' && *end != ',' && *end != ')' && *end != '\n' && *end != 0)
			end++;
		dstr_copy(&texture_name, "");
		dstr_ncat(&texture_name, start, end - start);

		dstr_copy(&replacing, "");
		dstr_ncat(&replacing, texture, end - texture);

		dstr_copy(&replacement, "");

		if (num_textures) {
			dstr_cat_dstr(&replacement, &texture_name);
			dstr_cat(&replacement, ".Sample(textureSampler");

			dstr_cat(&insert_text, "uniform texture2d ");
			dstr_cat(&insert_text, texture_name.array);
			dstr_cat(&insert_text, ";\n");
		} else {
			dstr_cat(&replacement, "image.Sample(textureSampler");
		}
		dstr_replace(&effect_text, replacing.array, replacement.array);

		dstr_copy(&replacing, "textureSize(");
		dstr_cat(&replacing, texture_name.array);
		dstr_cat(&replacing, ", 0)");

		dstr_replace(&effect_text, replacing.array, "uv_size");
		dstr_replace(&replacing, ", 0)", ",0)");
		dstr_replace(&effect_text, replacing.array, "uv_size");

		num_textures++;
		size_t prev_diff = texture_diff;
		texture = NULL;
		for (size_t i = 0; i < sizeof(texture_find) / sizeof(char *); i++) {
			char *t = strstr(effect_text.array + diff + prev_diff, texture_find[i]);
			if (t && (!texture || t < texture)) {
				texture = t;
				texture_diff = strlen(texture_find[i]);
			}
		}
	}
	dstr_free(&replacing);
	dstr_free(&replacement);
	dstr_free(&texture_name);

	if (insert_text.len > 24) {
		dstr_insert_dstr(&effect_text, 0, &insert_text);
	}
	dstr_free(&insert_text);

	obs_data_set_string(settings, "shader_text", effect_text.array);

	dstr_free(&effect_text);

	obs_data_release(settings);
	obs_property_set_visible(property, false);

	filter->reload_effect = true;

	obs_source_update(filter->context, NULL);
	return true;
}

static const char *shader_filter_texture_file_filter = "Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

static obs_properties_t *shader_filter_properties(void *data)
{
	struct shader_filter_data *filter = data;

	struct dstr examples_path = {0};
	dstr_init(&examples_path);
	dstr_cat(&examples_path, obs_get_module_data_path(obs_current_module()));
	dstr_cat(&examples_path, "/examples");

	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	if (!filter || !filter->transition) {
		obs_properties_add_int(props, "expand_left", obs_module_text("ShaderFilter.ExpandLeft"), 0, 9999, 1);
		obs_properties_add_int(props, "expand_right", obs_module_text("ShaderFilter.ExpandRight"), 0, 9999, 1);
		obs_properties_add_int(props, "expand_top", obs_module_text("ShaderFilter.ExpandTop"), 0, 9999, 1);
		obs_properties_add_int(props, "expand_bottom", obs_module_text("ShaderFilter.ExpandBottom"), 0, 9999, 1);
	}

	obs_properties_add_bool(props, "override_entire_effect", obs_module_text("ShaderFilter.OverrideEntireEffect"));

	obs_property_t *from_file = obs_properties_add_bool(props, "from_file", obs_module_text("ShaderFilter.LoadFromFile"));
	obs_property_set_modified_callback(from_file, shader_filter_from_file_changed);

	obs_property_t *shader_text =
		obs_properties_add_text(props, "shader_text", obs_module_text("ShaderFilter.ShaderText"), OBS_TEXT_MULTILINE);
	obs_property_set_modified_callback(shader_text, shader_filter_text_changed);

	obs_properties_add_button2(props, "shader_convert", obs_module_text("ShaderFilter.Convert"), shader_filter_convert, data);

	obs_property_t *file_name = obs_properties_add_path(props, "shader_file_name",
							    obs_module_text("ShaderFilter.ShaderFileName"), OBS_PATH_FILE, NULL,
							    examples_path.array);
	obs_property_set_modified_callback(file_name, shader_filter_file_name_changed);

	if (filter) {
		obs_data_t *settings = obs_source_get_settings(filter->context);
		const char *last_error = obs_data_get_string(settings, "last_error");
		if (last_error && strlen(last_error)) {
			obs_property_t *error =
				obs_properties_add_text(props, "last_error", obs_module_text("ShaderFilter.Error"), OBS_TEXT_INFO);
			obs_property_text_set_info_type(error, OBS_TEXT_INFO_ERROR);
		}
		obs_data_release(settings);
	}
	obs_property_t *use_shader_elapsed_time =
		obs_properties_add_bool(props, "use_shader_elapsed_time", obs_module_text("ShaderFilter.UseShaderElapsedTime"));
	obs_property_set_modified_callback(use_shader_elapsed_time, use_shader_elapsed_time_changed);

	obs_properties_add_button(props, "reload_effect", obs_module_text("ShaderFilter.ReloadEffect"),
				  shader_filter_reload_effect_clicked);

	DARRAY(obs_property_t *) groups;
	da_init(groups);

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param = (filter->stored_param_list.array + param_index);
		//gs_eparam_t *annot = gs_param_get_annotation_by_idx(param->param, param_index);
		const char *param_name = param->name.array;
		const char *label = param->display_name.array;
		const char *widget_type = param->widget_type.array;
		const char *group_name = param->group.array;
		const int *options = param->option_values.array;
		const struct dstr *option_labels = param->option_labels.array;

		struct dstr display_name = {0};
		struct dstr sources_name = {0};

		if (label == NULL) {
			dstr_ncat(&display_name, param_name, param->name.len);
			dstr_replace(&display_name, "_", " ");
		} else {
			dstr_ncat(&display_name, label, param->display_name.len);
		}
		obs_properties_t *group = NULL;
		if (group_name && strlen(group_name)) {
			for (size_t i = 0; i < groups.num; i++) {
				const char *n = obs_property_name(groups.array[i]);
				if (strcmp(n, group_name) == 0) {
					group = obs_property_group_content(groups.array[i]);
				}
			}
			if (!group) {
				group = obs_properties_create();
				obs_property_t *p =
					obs_properties_add_group(props, group_name, group_name, OBS_GROUP_NORMAL, group);
				da_push_back(groups, &p);
			}
		}
		if (!group)
			group = props;
		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			obs_properties_add_bool(group, param_name, display_name.array);
			break;
		case GS_SHADER_PARAM_FLOAT: {
			double range_min = param->minimum.f;
			double range_max = param->maximum.f;
			double step = param->step.f;
			if (range_min == range_max) {
				range_min = -1000.0;
				range_max = 1000.0;
				step = 0.0001;
			}
			obs_properties_remove_by_name(props, param_name);
			if (widget_type != NULL && strcmp(widget_type, "slider") == 0) {
				obs_properties_add_float_slider(group, param_name, display_name.array, range_min, range_max, step);
			} else {
				obs_properties_add_float(group, param_name, display_name.array, range_min, range_max, step);
			}
			break;
		}
		case GS_SHADER_PARAM_INT: {
			int range_min = (int)param->minimum.i;
			int range_max = (int)param->maximum.i;
			int step = (int)param->step.i;
			if (range_min == range_max) {
				range_min = -1000;
				range_max = 1000;
				step = 1;
			}
			obs_properties_remove_by_name(props, param_name);

			if (widget_type != NULL && strcmp(widget_type, "slider") == 0) {
				obs_properties_add_int_slider(group, param_name, display_name.array, range_min, range_max, step);
			} else if (widget_type != NULL && strcmp(widget_type, "select") == 0) {
				obs_property_t *plist = obs_properties_add_list(group, param_name, display_name.array,
										OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
				for (size_t i = 0; i < param->option_values.num; i++) {
					obs_property_list_add_int(plist, option_labels[i].array, options[i]);
				}
			} else {
				obs_properties_add_int(group, param_name, display_name.array, range_min, range_max, step);
			}
			break;
		}
		case GS_SHADER_PARAM_INT3:

			break;
		case GS_SHADER_PARAM_VEC2: {
			double range_min = param->minimum.f;
			double range_max = param->maximum.f;
			double step = param->step.f;
			if (range_min == range_max) {
				range_min = -1000.0;
				range_max = 1000.0;
				step = 0.0001;
			}

			bool slider = (widget_type != NULL && strcmp(widget_type, "slider") == 0);

			for (size_t i = 0; i < 2; i++) {
				dstr_printf(&sources_name, "%s_%zu", param_name, i);
				if (i < param->option_labels.num) {
					if (slider) {
						obs_properties_add_float_slider(group, sources_name.array,
										param->option_labels.array[i].array, range_min,
										range_max, step);
					} else {
						obs_properties_add_float(group, sources_name.array,
									 param->option_labels.array[i].array, range_min, range_max,
									 step);
					}
				} else if (slider) {

					obs_properties_add_float_slider(group, sources_name.array, display_name.array, range_min,
									range_max, step);
				} else {
					obs_properties_add_float(group, sources_name.array, display_name.array, range_min,
								 range_max, step);
				}
			}
			dstr_free(&sources_name);

			break;
		}
		case GS_SHADER_PARAM_VEC3:
			if (widget_type != NULL && strcmp(widget_type, "slider") == 0) {
				double range_min = param->minimum.f;
				double range_max = param->maximum.f;
				double step = param->step.f;
				if (range_min == range_max) {
					range_min = -1000.0;
					range_max = 1000.0;
					step = 0.0001;
				}
				for (size_t i = 0; i < 3; i++) {
					dstr_printf(&sources_name, "%s_%zu", param_name, i);
					if (i < param->option_labels.num) {
						obs_properties_add_float_slider(group, sources_name.array,
										param->option_labels.array[i].array, range_min,
										range_max, step);
					} else {
						obs_properties_add_float_slider(group, sources_name.array, display_name.array,
										range_min, range_max, step);
					}
				}
				dstr_free(&sources_name);
			} else {
				obs_properties_add_color(group, param_name, display_name.array);
			}
			break;
		case GS_SHADER_PARAM_VEC4:
			if (widget_type != NULL && strcmp(widget_type, "slider") == 0) {
				double range_min = param->minimum.f;
				double range_max = param->maximum.f;
				double step = param->step.f;
				if (range_min == range_max) {
					range_min = -1000.0;
					range_max = 1000.0;
					step = 0.0001;
				}
				for (size_t i = 0; i < 4; i++) {
					dstr_printf(&sources_name, "%s_%zu", param_name, i);
					if (i < param->option_labels.num) {
						obs_properties_add_float_slider(group, sources_name.array,
										param->option_labels.array[i].array, range_min,
										range_max, step);
					} else {
						obs_properties_add_float_slider(group, sources_name.array, display_name.array,
										range_min, range_max, step);
					}
				}
				dstr_free(&sources_name);
			} else {
				obs_properties_add_color_alpha(group, param_name, display_name.array);
			}
			break;
		case GS_SHADER_PARAM_TEXTURE:
			if (widget_type != NULL && strcmp(widget_type, "source") == 0) {
				dstr_init_copy_dstr(&sources_name, &param->name);
				dstr_cat(&sources_name, "_source");
				obs_property_t *p = obs_properties_add_list(group, sources_name.array, display_name.array,
									    OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
				dstr_free(&sources_name);
				obs_enum_sources(add_source_to_list, p);
				obs_enum_scenes(add_source_to_list, p);
				obs_property_list_insert_string(p, 0, "", "");

			} else if (widget_type != NULL && strcmp(widget_type, "file") == 0) {
				obs_properties_add_path(group, param_name, display_name.array, OBS_PATH_FILE,
							shader_filter_texture_file_filter, NULL);
			} else {
				dstr_init_copy_dstr(&sources_name, &param->name);
				dstr_cat(&sources_name, "_source");
				obs_property_t *p = obs_properties_add_list(group, sources_name.array, display_name.array,
									    OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
				dstr_free(&sources_name);
				obs_property_list_add_string(p, "", "");
				obs_enum_sources(add_source_to_list, p);
				obs_enum_scenes(add_source_to_list, p);
				obs_properties_add_path(group, param_name, display_name.array, OBS_PATH_FILE,
							shader_filter_texture_file_filter, NULL);
			}
			break;
		case GS_SHADER_PARAM_STRING:
			if (widget_type != NULL && strcmp(widget_type, "info") == 0) {
				obs_properties_add_text(group, param_name, display_name.array, OBS_TEXT_INFO);
			} else {
				obs_properties_add_text(group, param_name, display_name.array, OBS_TEXT_MULTILINE);
			}
			break;
		default:;
		}
		dstr_free(&display_name);
	}
	da_free(groups);
	dstr_free(&examples_path);

	obs_properties_add_text(
		props, "plugin_info",
		"<a href=\"https://obsproject.com/forum/resources/obs-shaderfilter.1736/\">obs-shaderfilter</a> (" PROJECT_VERSION
		") by <a href=\"https://www.exeldro.com\">Exeldro</a>",
		OBS_TEXT_INFO);
	return props;
}

static void shader_filter_update(void *data, obs_data_t *settings)
{
	struct shader_filter_data *filter = data;

	// Get expansions. Will be used in the video_tick() callback.

	filter->expand_left = (int)obs_data_get_int(settings, "expand_left");
	filter->expand_right = (int)obs_data_get_int(settings, "expand_right");
	filter->expand_top = (int)obs_data_get_int(settings, "expand_top");
	filter->expand_bottom = (int)obs_data_get_int(settings, "expand_bottom");
	filter->use_shader_elapsed_time = (bool)obs_data_get_bool(settings, "use_shader_elapsed_time");
	filter->rand_activation_f = (float)((double)rand_interval(0, 10000) / (double)10000);

	if (filter->reload_effect) {
		filter->reload_effect = false;
		shader_filter_reload_effect(filter);
		obs_source_update_properties(filter->context);
	}

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param = (filter->stored_param_list.array + param_index);
		//gs_eparam_t *annot = gs_param_get_annotation_by_idx(param->param, param_index);
		const char *param_name = param->name.array;
		struct dstr sources_name = {0};
		obs_source_t *source = NULL;
		void *default_value = gs_effect_get_default_val(param->param);
		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			if (default_value != NULL)
				obs_data_set_default_bool(settings, param_name, *(bool *)default_value);
			param->value.i = obs_data_get_bool(settings, param_name);
			break;
		case GS_SHADER_PARAM_FLOAT:
			if (default_value != NULL)
				obs_data_set_default_double(settings, param_name, *(float *)default_value);
			param->value.f = obs_data_get_double(settings, param_name);
			break;
		case GS_SHADER_PARAM_INT:
			if (default_value != NULL)
				obs_data_set_default_int(settings, param_name, *(int *)default_value);
			param->value.i = obs_data_get_int(settings, param_name);
			break;
		case GS_SHADER_PARAM_VEC2: {
			struct vec2 *xy = default_value;

			for (size_t i = 0; i < 2; i++) {
				dstr_printf(&sources_name, "%s_%zu", param_name, i);
				if (xy != NULL)
					obs_data_set_default_double(settings, sources_name.array, xy->ptr[i]);
				param->value.vec2.ptr[i] = (float)obs_data_get_double(settings, sources_name.array);
			}
			dstr_free(&sources_name);
			break;
		}
		case GS_SHADER_PARAM_VEC3: {
			struct vec3 *rgb = default_value;
			if (param->widget_type.array && strcmp(param->widget_type.array, "slider") == 0) {
				for (size_t i = 0; i < 3; i++) {
					dstr_printf(&sources_name, "%s_%zu", param_name, i);
					if (rgb != NULL)
						obs_data_set_default_double(settings, sources_name.array, rgb->ptr[i]);
					param->value.vec3.ptr[i] = (float)obs_data_get_double(settings, sources_name.array);
				}
				dstr_free(&sources_name);
			} else {
				if (rgb != NULL) {
					struct vec4 rgba;
					vec4_from_vec3(&rgba, rgb);
					obs_data_set_default_int(settings, param_name, vec4_to_rgba(&rgba));
				} else {
					// Hack to ensure we have a default...(white)
					obs_data_set_default_int(settings, param_name, 0xffffffff);
				}
				vec4_from_rgba(&param->value.vec4, (uint32_t)obs_data_get_int(settings, param_name));
			}
			break;
		}
		case GS_SHADER_PARAM_VEC4: {
			struct vec4 *rgba = default_value;
			if (param->widget_type.array && strcmp(param->widget_type.array, "slider") == 0) {
				for (size_t i = 0; i < 4; i++) {
					dstr_printf(&sources_name, "%s_%zu", param_name, i);
					if (rgba != NULL)
						obs_data_set_default_double(settings, sources_name.array, rgba->ptr[i]);
					param->value.vec4.ptr[i] = (float)obs_data_get_double(settings, sources_name.array);
				}
				dstr_free(&sources_name);
			} else {
				if (rgba != NULL) {
					obs_data_set_default_int(settings, param_name, vec4_to_rgba(rgba));
				} else {
					// Hack to ensure we have a default...(white)
					obs_data_set_default_int(settings, param_name, 0xffffffff);
				}
				vec4_from_rgba(&param->value.vec4, (uint32_t)obs_data_get_int(settings, param_name));
			}
			break;
		}
		case GS_SHADER_PARAM_TEXTURE:
			dstr_init_copy_dstr(&sources_name, &param->name);
			dstr_cat(&sources_name, "_source");
			const char *sn = obs_data_get_string(settings, sources_name.array);
			dstr_free(&sources_name);
			source = obs_weak_source_get_source(param->source);
			if (source && strcmp(obs_source_get_name(source), sn) != 0) {
				obs_source_release(source);
				source = NULL;
			}
			if (!source)
				source = (sn && strlen(sn)) ? obs_get_source_by_name(sn) : NULL;
			if (source) {
				if (!obs_weak_source_references_source(param->source, source)) {
					if ((!filter->transition || filter->prev_transitioning) &&
					    obs_source_active(filter->context))
						obs_source_inc_active(source);
					if ((!filter->transition || filter->prev_transitioning) &&
					    obs_source_showing(filter->context))
						obs_source_inc_showing(source);

					obs_source_t *old_source = obs_weak_source_get_source(param->source);
					if (old_source) {
						if ((!filter->transition || filter->prev_transitioning) &&
						    obs_source_active(filter->context))
							obs_source_dec_active(old_source);
						if ((!filter->transition || filter->prev_transitioning) &&
						    obs_source_showing(filter->context))
							obs_source_dec_showing(old_source);
						obs_source_release(old_source);
					}
					obs_weak_source_release(param->source);
					param->source = obs_source_get_weak_source(source);
				}
				obs_source_release(source);
				if (param->image) {
					gs_image_file_free(param->image);
					param->image = NULL;
				}
				dstr_free(&param->path);
			} else {
				const char *path = default_value;
				if (!obs_data_has_user_value(settings, param_name) && path && strlen(path)) {
					if (os_file_exists(path)) {
						char *abs_path = os_get_abs_path_ptr(path);
						obs_data_set_default_string(settings, param_name, abs_path);
						bfree(abs_path);
					} else {
						struct dstr texture_path = {0};
						dstr_init(&texture_path);
						dstr_cat(&texture_path, obs_get_module_data_path(obs_current_module()));
						dstr_cat(&texture_path, "/textures/");
						dstr_cat(&texture_path, path);
						char *abs_path = os_get_abs_path_ptr(texture_path.array);
						if (os_file_exists(abs_path)) {
							obs_data_set_default_string(settings, param_name, abs_path);
						}
						bfree(abs_path);
						dstr_free(&texture_path);
					}
				}
				path = obs_data_get_string(settings, param_name);
				bool n = false;
				if (param->image == NULL) {
					param->image = bzalloc(sizeof(gs_image_file_t));
					n = true;
				}
				if (n || !path || !param->path.array || strcmp(path, param->path.array) != 0) {

					if (!n) {
						obs_enter_graphics();
						gs_image_file_free(param->image);
						obs_leave_graphics();
					}
					gs_image_file_init(param->image, path);
					dstr_copy(&param->path, path);
					obs_enter_graphics();
					gs_image_file_init_texture(param->image);
					obs_leave_graphics();
				}
				obs_source_t *old_source = obs_weak_source_get_source(param->source);
				if (old_source) {
					if ((!filter->transition || filter->prev_transitioning) &&
					    obs_source_active(filter->context))
						obs_source_dec_active(old_source);
					if ((!filter->transition || filter->prev_transitioning) &&
					    obs_source_showing(filter->context))
						obs_source_dec_showing(old_source);
					obs_source_release(old_source);
				}
				obs_weak_source_release(param->source);
				param->source = NULL;
			}
			break;
		case GS_SHADER_PARAM_STRING:
			if (default_value != NULL)
				obs_data_set_default_string(settings, param_name, (const char *)default_value);
			param->value.string = (char *)obs_data_get_string(settings, param_name);
			break;
		default:;
		}
		bfree(default_value);
	}
}

static void shader_filter_tick(void *data, float seconds)
{
	struct shader_filter_data *filter = data;
	obs_source_t *target = filter->transition ? filter->context : obs_filter_get_target(filter->context);
	if (!target)
		return;
	// Determine offsets from expansion values.
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);

	filter->total_width = filter->expand_left + base_width + filter->expand_right;
	filter->total_height = filter->expand_top + base_height + filter->expand_bottom;

	filter->uv_size.x = (float)filter->total_width;
	filter->uv_size.y = (float)filter->total_height;

	filter->uv_scale.x = (float)filter->total_width / base_width;
	filter->uv_scale.y = (float)filter->total_height / base_height;

	filter->uv_offset.x = (float)(-filter->expand_left) / base_width;
	filter->uv_offset.y = (float)(-filter->expand_top) / base_height;

	filter->uv_pixel_interval.x = 1.0f / base_width;
	filter->uv_pixel_interval.y = 1.0f / base_height;

	if (filter->shader_start_time == 0) {
		filter->shader_start_time = filter->elapsed_time + seconds;
	}
	filter->elapsed_time += seconds;
	filter->elapsed_time_loop += seconds;
	if (filter->elapsed_time_loop > 1.) {
		filter->elapsed_time_loop -= 1.;

		// Loops
		filter->loops += 1;
		if (filter->loops >= 4194304)
			filter->loops = -filter->loops;
	}
	filter->local_time = (float)(os_gettime_ns() / 1000000000.0);

	// undecided between this and "rand_float(1);"
	filter->rand_f = (float)((double)rand_interval(0, 10000) / (double)10000);

	filter->rendered = false;
}

gs_texrender_t *create_or_reset_texrender(gs_texrender_t *render)
{
	if (!render) {
		render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else {
		gs_texrender_reset(render);
	}
	return render;
}

static void get_input_source(struct shader_filter_data *filter)
{
	// Use the OBS default effect file as our effect.
	gs_effect_t *pass_through = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	// Set up our color space info.
	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	const enum gs_color_space source_space =
		obs_source_get_color_space(obs_filter_get_target(filter->context), OBS_COUNTOF(preferred_spaces), preferred_spaces);

	const enum gs_color_format format = gs_get_format_from_space(source_space);

	// Set up our input_texrender to catch the output texture.
	filter->input_texrender = create_or_reset_texrender(filter->input_texrender);

	// Start the rendering process with our correct color space params,
	// And set up your texrender to recieve the created texture.
	if (!filter->transition &&
		!obs_source_process_filter_begin_with_color_space(filter->context, format, source_space, OBS_NO_DIRECT_RENDERING))
		return;

	if (gs_texrender_begin(filter->input_texrender, filter->total_width, filter->total_height)) {

		gs_blend_state_push();
		gs_reset_blend_state();
		gs_blend_op(GS_BLEND_OP_ADD);
		gs_enable_blending(false);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		gs_ortho(0.0f, (float)filter->total_width, 0.0f, (float)filter->total_height, -100.0f, 100.0f);
		// The incoming source is pre-multiplied alpha, so use the
		// OBS default effect "DrawAlphaDivide" technique to convert
		// the colors back into non-pre-multiplied space. If the shader
		// file has #define USE_PM_ALPHA 1, then use normal "Draw"
		// technique.
		const char *technique = filter->use_pm_alpha ? "Draw" : "DrawAlphaDivide";
		if (!filter->transition)
			obs_source_process_filter_tech_end(filter->context, pass_through, filter->total_width, filter->total_height,
							   technique);
		gs_texrender_end(filter->input_texrender);
		gs_blend_state_pop();
	}
}

static void draw_output(struct shader_filter_data *filter)
{
	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	const enum gs_color_space source_space =
		obs_source_get_color_space(obs_filter_get_target(filter->context), OBS_COUNTOF(preferred_spaces), preferred_spaces);

	const enum gs_color_format format = gs_get_format_from_space(source_space);

	if (!obs_source_process_filter_begin_with_color_space(filter->context, format, source_space, OBS_NO_DIRECT_RENDERING)) {
		return;
	}

	gs_texture_t *texture = gs_texrender_get_texture(filter->output_texrender);
	gs_effect_t *pass_through = filter->output_effect;

	if (filter->param_output_image) {
		gs_effect_set_texture(filter->param_output_image, texture);
	}

	obs_source_process_filter_end(filter->context, pass_through, filter->total_width, filter->total_height);
}

void shader_filter_set_effect_params(struct shader_filter_data *filter)
{

	if (filter->param_uv_scale != NULL) {
		gs_effect_set_vec2(filter->param_uv_scale, &filter->uv_scale);
	}
	if (filter->param_uv_offset != NULL) {
		gs_effect_set_vec2(filter->param_uv_offset, &filter->uv_offset);
	}
	if (filter->param_uv_pixel_interval != NULL) {
		gs_effect_set_vec2(filter->param_uv_pixel_interval, &filter->uv_pixel_interval);
	}
	if (filter->param_elapsed_time != NULL) {
		if (filter->use_shader_elapsed_time) {
			gs_effect_set_float(filter->param_elapsed_time, filter->elapsed_time - filter->shader_start_time);
		} else {
			gs_effect_set_float(filter->param_elapsed_time, filter->elapsed_time);
		}
	}
	if (filter->param_uv_size != NULL) {
		gs_effect_set_vec2(filter->param_uv_size, &filter->uv_size);
	}
	if (filter->param_local_time != NULL) {
		gs_effect_set_float(filter->param_local_time, filter->local_time);
	}
	if (filter->param_loops != NULL) {
		gs_effect_set_int(filter->param_loops, filter->loops);
	}
	if (filter->param_rand_f != NULL) {
		gs_effect_set_float(filter->param_rand_f, filter->rand_f);
	}
	if (filter->param_rand_activation_f != NULL) {
		gs_effect_set_float(filter->param_rand_activation_f, filter->rand_activation_f);
	}
	if (filter->param_rand_instance_f != NULL) {
		gs_effect_set_float(filter->param_rand_instance_f, filter->rand_instance_f);
	}

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param = (filter->stored_param_list.array + param_index);
		obs_source_t *source = NULL;

		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			gs_effect_set_bool(param->param, param->value.i);
			break;
		case GS_SHADER_PARAM_FLOAT:
			gs_effect_set_float(param->param, (float)param->value.f);
			break;
		case GS_SHADER_PARAM_INT:
			gs_effect_set_int(param->param, (int)param->value.i);
			break;
		case GS_SHADER_PARAM_VEC2:
			gs_effect_set_vec2(param->param, &param->value.vec2);
			break;
		case GS_SHADER_PARAM_VEC3:
			gs_effect_set_vec3(param->param, &param->value.vec3);
			break;
		case GS_SHADER_PARAM_VEC4:
			gs_effect_set_vec4(param->param, &param->value.vec4);
			break;
		case GS_SHADER_PARAM_TEXTURE:
			source = obs_weak_source_get_source(param->source);
			if (source) {
				const enum gs_color_space preferred_spaces[] = {
					GS_CS_SRGB,
					GS_CS_SRGB_16F,
					GS_CS_709_EXTENDED,
				};
				const enum gs_color_space space =
					obs_source_get_color_space(source, OBS_COUNTOF(preferred_spaces), preferred_spaces);
				const enum gs_color_format format = gs_get_format_from_space(space);
				if (!param->render || gs_texrender_get_format(param->render) != format) {
					gs_texrender_destroy(param->render);
					param->render = gs_texrender_create(format, GS_ZS_NONE);
				} else {
					gs_texrender_reset(param->render);
				}
				uint32_t base_width = obs_source_get_base_width(source);
				uint32_t base_height = obs_source_get_base_height(source);
				gs_blend_state_push();
				gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
				if (gs_texrender_begin_with_color_space(param->render, base_width, base_height, space)) {
					const float w = (float)base_width;
					const float h = (float)base_height;
					uint32_t flags = obs_source_get_output_flags(source);
					const bool custom_draw = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
					const bool async = (flags & OBS_SOURCE_ASYNC) != 0;
					struct vec4 clear_color;

					vec4_zero(&clear_color);
					gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
					gs_ortho(0.0f, w, 0.0f, h, -100.0f, 100.0f);

					if (!custom_draw && !async)
						obs_source_default_render(source);
					else
						obs_source_video_render(source);
					gs_texrender_end(param->render);
				}
				gs_blend_state_pop();
				obs_source_release(source);
				gs_texture_t *tex = gs_texrender_get_texture(param->render);
				gs_effect_set_texture(param->param, tex);
			} else if (param->image) {
				gs_effect_set_texture(param->param, param->image->texture);
			} else {
				gs_effect_set_texture(param->param, NULL);
			}

			break;
		case GS_SHADER_PARAM_STRING:
			gs_effect_set_val(param->param, (param->value.string ? param->value.string : NULL),
					  gs_effect_get_val_size(param->param));
			break;
		default:;
		}
	}
}

static void render_shader(struct shader_filter_data *filter)
{
	gs_texture_t *texture = gs_texrender_get_texture(filter->input_texrender);
	if (!texture) {
		return;
	}

	filter->output_texrender = create_or_reset_texrender(filter->output_texrender);

	if (filter->param_image) {
		gs_effect_set_texture(filter->param_image, texture);
	}
	shader_filter_set_effect_params(filter);

	gs_blend_state_push();
	gs_reset_blend_state();
	gs_blend_op(GS_BLEND_OP_ADD);
	gs_enable_blending(false);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->output_texrender, filter->total_width, filter->total_height)) {
		gs_ortho(0.0f, (float)filter->total_width, 0.0f, (float)filter->total_height, -100.0f, 100.0f);
		bool first_iteration = true;
		while (gs_effect_loop(filter->effect, "Draw")) {
			gs_draw_sprite(texture, 0, filter->total_width, filter->total_height);
			if (first_iteration) {
				gs_enable_blending(true);
				gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
				first_iteration = false;
			}
		}
		gs_texrender_end(filter->output_texrender);
	}

	gs_blend_state_pop();
}

static void shader_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct shader_filter_data *filter = data;

	if (filter->rendered) {
		draw_output(filter);
		return;
	}

	if (filter->effect == NULL || filter->rendering) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	get_input_source(filter);

	filter->rendering = true;
	render_shader(filter);
	draw_output(filter);
	filter->rendered = true;
	filter->rendering = false;
}
static uint32_t shader_filter_getwidth(void *data)
{
	struct shader_filter_data *filter = data;

	return filter->total_width;
}

static uint32_t shader_filter_getheight(void *data)
{
	struct shader_filter_data *filter = data;

	return filter->total_height;
}

static void shader_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "shader_text", effect_template_default_image_shader);
}

static enum gs_color_space shader_filter_get_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	struct shader_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	const enum gs_color_space potential_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};
	return obs_source_get_color_space(target, OBS_COUNTOF(potential_spaces), potential_spaces);
}

void shader_filter_param_source_action(void *data, void (*action)(obs_source_t *source))
{
	struct shader_filter_data *filter = data;
	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param = (filter->stored_param_list.array + param_index);
		if (!param->source)
			continue;
		obs_source_t *source = obs_weak_source_get_source(param->source);
		if (!source)
			continue;
		action(source);
		obs_source_release(source);
	}
}

void shader_filter_activate(void *data)
{
	shader_filter_param_source_action(data, obs_source_inc_active);
}

void shader_filter_deactivate(void *data)
{
	shader_filter_param_source_action(data, obs_source_dec_active);
}

void shader_filter_show(void *data)
{
	shader_filter_param_source_action(data, obs_source_inc_showing);
}

void shader_filter_hide(void *data)
{
	shader_filter_param_source_action(data, obs_source_dec_showing);
}

struct obs_source_info shader_filter = {
	.id = "shader_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB | OBS_SOURCE_CUSTOM_DRAW,
	.create = shader_filter_create,
	.destroy = shader_filter_destroy,
	.update = shader_filter_update,
	.load = shader_filter_update,
	.video_tick = shader_filter_tick,
	.get_name = shader_filter_get_name,
	.get_defaults = shader_filter_defaults,
	.get_width = shader_filter_getwidth,
	.get_height = shader_filter_getheight,
	.video_render = shader_filter_render,
	.get_properties = shader_filter_properties,
	.video_get_color_space = shader_filter_get_color_space,
	.activate = shader_filter_activate,
	.deactivate = shader_filter_deactivate,
	.show = shader_filter_show,
	.hide = shader_filter_hide,
};

static void *shader_transition_create(obs_data_t *settings, obs_source_t *source)
{
	struct shader_filter_data *filter = bzalloc(sizeof(struct shader_filter_data));
	filter->context = source;
	filter->reload_effect = true;
	filter->transition = true;

	dstr_init(&filter->last_path);
	dstr_copy(&filter->last_path, obs_data_get_string(settings, "shader_file_name"));
	filter->last_from_file = obs_data_get_bool(settings, "from_file");
	filter->rand_instance_f = (float)((double)rand_interval(0, 10000) / (double)10000);
	filter->rand_activation_f = (float)((double)rand_interval(0, 10000) / (double)10000);

	da_init(filter->stored_param_list);

	obs_source_update(source, settings);

	return filter;
}

static const char *shader_transition_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ShaderFilter");
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - t;
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t;
}

static bool shader_transition_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
					   size_t channels, size_t sample_rate)
{
	struct shader_filter_data *filter = data;
	return obs_transition_audio_render(filter->context, ts_out, audio, mixers, channels, sample_rate, mix_a, mix_b);
}

static void shader_transition_video_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	if (!a && !b)
		return;

	struct shader_filter_data *filter = data;
	if (filter->effect == NULL || filter->rendering)
		return;

	if (!filter->prev_transitioning) {
		if (obs_source_active(filter->context))
			shader_filter_param_source_action(data, obs_source_inc_active);
		if (obs_source_showing(filter->context))
			shader_filter_param_source_action(data, obs_source_inc_showing);
	}
	filter->transitioning = true;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	if (gs_get_color_space() == GS_CS_SRGB) {
		if (filter->param_image_a != NULL)
			gs_effect_set_texture(filter->param_image_a, a);
		if (filter->param_image_b != NULL)
			gs_effect_set_texture(filter->param_image_b, b);
		if (filter->param_image != NULL)
			gs_effect_set_texture(filter->param_image, t < 0.5 ? a : b);
		if (filter->param_convert_linear)
			gs_effect_set_bool(filter->param_convert_linear, true);
	} else {
		if (filter->param_image_a != NULL)
			gs_effect_set_texture_srgb(filter->param_image_a, a);
		if (filter->param_image_b != NULL)
			gs_effect_set_texture_srgb(filter->param_image_b, b);
		if (filter->param_image != NULL)
			gs_effect_set_texture_srgb(filter->param_image, t < 0.5 ? a : b);
		if (filter->param_convert_linear)
			gs_effect_set_bool(filter->param_convert_linear, false);
	}
	if (filter->param_transition_time != NULL)
		gs_effect_set_float(filter->param_transition_time, t);

	shader_filter_set_effect_params(filter);

	while (gs_effect_loop(filter->effect, "Draw"))
		gs_draw_sprite(NULL, 0, cx, cy);

	gs_enable_framebuffer_srgb(previous);
}

static void shader_transition_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	const bool previous = gs_set_linear_srgb(true);

	struct shader_filter_data *filter = data;
	filter->transitioning = false;
	obs_transition_video_render2(filter->context, shader_transition_video_callback, NULL);
	if (!filter->transitioning && filter->prev_transitioning) {
		if (obs_source_active(filter->context))
			shader_filter_param_source_action(data, obs_source_dec_active);
		if (obs_source_showing(filter->context))
			shader_filter_param_source_action(data, obs_source_dec_showing);
	}
	filter->prev_transitioning = filter->transitioning;
	gs_set_linear_srgb(previous);
}

static void shader_transition_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "shader_text", effect_template_default_transition_image_shader);
}

static enum gs_color_space shader_transition_get_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces)
{
	struct shader_filter_data *filter = data;
	const enum gs_color_space transition_space = obs_transition_video_get_color_space(filter->context);

	enum gs_color_space space = transition_space;
	for (size_t i = 0; i < count; ++i) {
		space = preferred_spaces[i];
		if (space == transition_space)
			break;
	}

	return space;
}

struct obs_source_info shader_transition = {
	.id = "shader_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.create = shader_transition_create,
	.destroy = shader_filter_destroy,
	.update = shader_filter_update,
	.load = shader_filter_update,
	.video_tick = shader_filter_tick,
	.get_name = shader_transition_get_name,
	.audio_render = shader_transition_audio_render,
	.get_defaults = shader_transition_defaults,
	.video_render = shader_transition_video_render,
	.get_properties = shader_filter_properties,
	.video_get_color_space = shader_transition_get_color_space,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-shaderfilter", "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-shaderfilter] loaded version %s", PROJECT_VERSION);
	obs_register_source(&shader_filter);
	obs_register_source(&shader_transition);

	return true;
}

void obs_module_unload(void) {}
