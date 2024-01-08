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

static char *
load_shader_from_file(const char *file_name) // add input of visited files
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
			char *abs_include_path =
				os_get_abs_path_ptr(include_path.array);
			char *file_contents =
				load_shader_from_file(abs_include_path);
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
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		if (param->image) {
			obs_enter_graphics();
			gs_image_file_free(param->image);
			obs_leave_graphics();

			bfree(param->image);
			param->image = NULL;
		}
		if (param->source) {
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
		blog(LOG_WARNING,
		     "[obs-shaderfilter] Unable to load output.effect file.  Errors:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)"
							    : errors));
		bfree(errors);
	} else {
		size_t effect_count =
			gs_effect_get_num_params(filter->output_effect);
		for (size_t effect_index = 0; effect_index < effect_count;
		     effect_index++) {
			gs_eparam_t *param = gs_effect_get_param_by_idx(
				filter->output_effect, effect_index);
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
	bool use_template =
		!obs_data_get_bool(settings, "override_entire_effect");

	if (obs_data_get_bool(settings, "from_file")) {
		const char *file_name =
			obs_data_get_string(settings, "shader_file_name");
		shader_text = load_shader_from_file(file_name);
	} else {
		shader_text =
			bstrdup(obs_data_get_string(settings, "shader_text"));
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
		blog(LOG_WARNING,
		     "[obs-shaderfilter] Unable to create effect. Errors returned from parser:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)"
							    : errors));
		if (errors && strlen(errors)) {
			obs_data_set_string(settings, "last_error", errors);
		} else {
			obs_data_set_string(
				settings, "last_error",
				obs_module_text("ShaderFilter.Unknown"));
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
	for (size_t effect_index = 0; effect_index < effect_count;
	     effect_index++) {
		gs_eparam_t *param = gs_effect_get_param_by_idx(filter->effect,
								effect_index);

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
		} else if (filter->transition &&
			   strcmp(info.name, "image_a") == 0) {
			filter->param_image_a = param;
		} else if (filter->transition &&
			   strcmp(info.name, "image_b") == 0) {
			filter->param_image_b = param;
		} else if (filter->transition &&
			   strcmp(info.name, "transition_time") == 0) {
			filter->param_transition_time = param;
		} else if (filter->transition &&
			   strcmp(info.name, "convert_linear") == 0) {
			filter->param_convert_linear = param;
		} else {
			struct effect_param_data *cached_data =
				da_push_back_new(filter->stored_param_list);
			dstr_copy(&cached_data->name, info.name);
			cached_data->type = info.type;
			cached_data->param = param;
			da_init(cached_data->option_values);
			da_init(cached_data->option_labels);
			const size_t annotation_count =
				gs_param_get_num_annotations(param);
			for (size_t annotation_index = 0;
			     annotation_index < annotation_count;
			     annotation_index++) {
				gs_eparam_t *annotation =
					gs_param_get_annotation_by_idx(
						param, annotation_index);
				void *annotation_default =
					gs_effect_get_default_val(annotation);
				gs_effect_get_param_info(annotation, &info);
				if (strcmp(info.name, "name") == 0 &&
				    info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->display_name,
						  (const char *)
							  annotation_default);
				} else if (strcmp(info.name, "label") == 0 &&
					   info.type ==
						   GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->display_name,
						  (const char *)
							  annotation_default);
				} else if (strcmp(info.name, "widget_type") ==
						   0 &&
					   info.type ==
						   GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->widget_type,
						  (const char *)
							  annotation_default);
				} else if (strcmp(info.name, "group") == 0 &&
					   info.type ==
						   GS_SHADER_PARAM_STRING) {
					dstr_copy(&cached_data->group,
						  (const char *)
							  annotation_default);
				} else if (strcmp(info.name, "minimum") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->minimum.f = *(
							float *)annotation_default;
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->minimum.i = *(
							int *)annotation_default;
					}
				} else if (strcmp(info.name, "maximum") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->maximum.f = *(
							float *)annotation_default;
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->maximum.i = *(
							int *)annotation_default;
					}
				} else if (strcmp(info.name, "step") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->step.f = *(
							float *)annotation_default;
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->step.i = *(
							int *)annotation_default;
					}
				} else if (strncmp(info.name, "option_", 7) ==
					   0) {
					int id = atoi(info.name + 7);
					if (info.type == GS_SHADER_PARAM_INT) {
						int val = *(
							int *)annotation_default;
						int *cd = da_insert_new(
							cached_data
								->option_values,
							id);
						*cd = val;

					} else if (info.type ==
						   GS_SHADER_PARAM_STRING) {
						struct dstr val = {0};
						dstr_copy(
							&val,
							(const char *)
								annotation_default);
						struct dstr *cs = da_insert_new(
							cached_data
								->option_labels,
							id);
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
	struct shader_filter_data *filter =
		bzalloc(sizeof(struct shader_filter_data));
	filter->context = source;
	filter->reload_effect = true;

	dstr_init(&filter->last_path);
	dstr_copy(&filter->last_path,
		  obs_data_get_string(settings, "shader_file_name"));
	filter->last_from_file = obs_data_get_bool(settings, "from_file");
	filter->rand_instance_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);
	filter->rand_activation_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);

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

static bool shader_filter_from_file_changed(obs_properties_t *props,
					    obs_property_t *p,
					    obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);

	bool from_file = obs_data_get_bool(settings, "from_file");

	obs_property_set_visible(obs_properties_get(props, "shader_text"),
				 !from_file);
	obs_property_set_visible(obs_properties_get(props, "shader_file_name"),
				 from_file);

	if (from_file != filter->last_from_file) {
		filter->reload_effect = true;
	}
	filter->last_from_file = from_file;

	return true;
}

static bool shader_filter_file_name_changed(obs_properties_t *props,
					    obs_property_t *p,
					    obs_data_t *settings)
{
	struct shader_filter_data *filter = obs_properties_get_param(props);
	const char *new_file_name =
		obs_data_get_string(settings, obs_property_name(p));

	if (dstr_is_empty(&filter->last_path) ||
	    dstr_cmp(&filter->last_path, new_file_name) != 0) {
		filter->reload_effect = true;
		dstr_copy(&filter->last_path, new_file_name);
		size_t l = strlen(new_file_name);
		if (l > 7 &&
		    strncmp(new_file_name + l - 7, ".effect", 7) == 0) {
			obs_data_set_bool(settings, "override_entire_effect",
					  true);
		} else if (l > 7 &&
			   strncmp(new_file_name + l - 7, ".shader", 7) == 0) {
			obs_data_set_bool(settings, "override_entire_effect",
					  false);
		}
	}

	return false;
}

static bool use_shader_elapsed_time_changed(obs_properties_t *props,
					    obs_property_t *p,
					    obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);

	bool use_shader_elapsed_time =
		obs_data_get_bool(settings, "use_shader_elapsed_time");
	if (use_shader_elapsed_time != filter->use_shader_elapsed_time) {
		filter->reload_effect = true;
	}
	filter->use_shader_elapsed_time = use_shader_elapsed_time;

	return false;
}
static bool shader_filter_reload_effect_clicked(obs_properties_t *props,
						obs_property_t *property,
						void *data)
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
	while (idx < count &&
	       strcmp(name, obs_property_list_item_string(p, idx)) > 0)
		idx++;
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

static const char *shader_filter_texture_file_filter =
	"Textures (*.bmp *.tga *.png *.jpeg *.jpg *.gif);;";

static obs_properties_t *shader_filter_properties(void *data)
{
	struct shader_filter_data *filter = data;

	struct dstr examples_path = {0};
	dstr_init(&examples_path);
	dstr_cat(&examples_path,
		 obs_get_module_data_path(obs_current_module()));
	dstr_cat(&examples_path, "/examples");

	obs_properties_t *props = obs_properties_create();
	obs_properties_set_param(props, filter, NULL);

	if (!filter || !filter->transition) {
		obs_properties_add_int(
			props, "expand_left",
			obs_module_text("ShaderFilter.ExpandLeft"), 0, 9999, 1);
		obs_properties_add_int(
			props, "expand_right",
			obs_module_text("ShaderFilter.ExpandRight"), 0, 9999,
			1);
		obs_properties_add_int(
			props, "expand_top",
			obs_module_text("ShaderFilter.ExpandTop"), 0, 9999, 1);
		obs_properties_add_int(
			props, "expand_bottom",
			obs_module_text("ShaderFilter.ExpandBottom"), 0, 9999,
			1);
	}

	obs_properties_add_bool(
		props, "override_entire_effect",
		obs_module_text("ShaderFilter.OverrideEntireEffect"));

	obs_property_t *from_file = obs_properties_add_bool(
		props, "from_file",
		obs_module_text("ShaderFilter.LoadFromFile"));
	obs_property_set_modified_callback(from_file,
					   shader_filter_from_file_changed);

	obs_properties_add_text(props, "shader_text",
				obs_module_text("ShaderFilter.ShaderText"),
				OBS_TEXT_MULTILINE);

	obs_property_t *file_name = obs_properties_add_path(
		props, "shader_file_name",
		obs_module_text("ShaderFilter.ShaderFileName"), OBS_PATH_FILE,
		NULL, examples_path.array);
	obs_property_set_modified_callback(file_name,
					   shader_filter_file_name_changed);

	if (filter) {
		obs_data_t *settings = obs_source_get_settings(filter->context);
		const char *last_error =
			obs_data_get_string(settings, "last_error");
		if (last_error && strlen(last_error)) {
			obs_property_t *error = obs_properties_add_text(
				props, "last_error",
				obs_module_text("ShaderFilter.Error"),
				OBS_TEXT_INFO);
			obs_property_text_set_info_type(error,
							OBS_TEXT_INFO_ERROR);
		}
		obs_data_release(settings);
	}
	obs_property_t *use_shader_elapsed_time = obs_properties_add_bool(
		props, "use_shader_elapsed_time",
		obs_module_text("ShaderFilter.UseShaderElapsedTime"));
	obs_property_set_modified_callback(use_shader_elapsed_time,
					   use_shader_elapsed_time_changed);

	obs_properties_add_button(props, "reload_effect",
				  obs_module_text("ShaderFilter.ReloadEffect"),
				  shader_filter_reload_effect_clicked);

	DARRAY(obs_property_t *) groups;
	da_init(groups);

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
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
			dstr_ncat(&display_name, label,
				  param->display_name.len);
		}
		obs_properties_t *group = NULL;
		if (group_name && strlen(group_name)) {
			for (size_t i = 0; i < groups.num; i++) {
				const char *n =
					obs_property_name(groups.array[i]);
				if (strcmp(n, group_name) == 0) {
					group = obs_property_group_content(
						groups.array[i]);
				}
			}
			if (!group) {
				group = obs_properties_create();
				obs_property_t *p = obs_properties_add_group(
					props, group_name, group_name,
					OBS_GROUP_NORMAL, group);
				da_push_back(groups, &p);
			}
		}
		if (!group)
			group = props;
		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			obs_properties_add_bool(group, param_name,
						display_name.array);
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
			if (widget_type != NULL &&
			    strcmp(widget_type, "slider") == 0) {
				obs_properties_add_float_slider(
					group, param_name, display_name.array,
					range_min, range_max, step);
			} else {
				obs_properties_add_float(group, param_name,
							 display_name.array,
							 range_min, range_max,
							 step);
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

			if (widget_type != NULL &&
			    strcmp(widget_type, "slider") == 0) {
				obs_properties_add_int_slider(
					group, param_name, display_name.array,
					range_min, range_max, step);
			} else if (widget_type != NULL &&
				   strcmp(widget_type, "select") == 0) {
				obs_property_t *plist = obs_properties_add_list(
					group, param_name, display_name.array,
					OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);
				for (size_t i = 0; i < param->option_values.num;
				     i++) {
					obs_property_list_add_int(
						plist, option_labels[i].array,
						options[i]);
				}
			} else {
				obs_properties_add_int(group, param_name,
						       display_name.array,
						       range_min, range_max,
						       step);
			}
			break;
		}
		case GS_SHADER_PARAM_INT3:

			break;
		case GS_SHADER_PARAM_VEC4:
			obs_properties_add_color_alpha(group, param_name,
						       display_name.array);
			break;
		case GS_SHADER_PARAM_TEXTURE:
			if (widget_type != NULL &&
			    strcmp(widget_type, "source") == 0) {
				dstr_init_copy_dstr(&sources_name,
						    &param->name);
				dstr_cat(&sources_name, "_source");
				obs_property_t *p = obs_properties_add_list(
					group, sources_name.array,
					display_name.array,
					OBS_COMBO_TYPE_EDITABLE,
					OBS_COMBO_FORMAT_STRING);
				dstr_free(&sources_name);
				obs_enum_sources(add_source_to_list, p);
				obs_enum_scenes(add_source_to_list, p);
				obs_property_list_insert_string(p, 0, "", "");

			} else if (widget_type != NULL &&
				   strcmp(widget_type, "file") == 0) {
				obs_properties_add_path(
					group, param_name, display_name.array,
					OBS_PATH_FILE,
					shader_filter_texture_file_filter,
					NULL);
			} else {
				dstr_init_copy_dstr(&sources_name,
						    &param->name);
				dstr_cat(&sources_name, "_source");
				obs_property_t *p = obs_properties_add_list(
					group, sources_name.array,
					display_name.array,
					OBS_COMBO_TYPE_EDITABLE,
					OBS_COMBO_FORMAT_STRING);
				dstr_free(&sources_name);
				obs_property_list_add_string(p, "", "");
				obs_enum_sources(add_source_to_list, p);
				obs_enum_scenes(add_source_to_list, p);
				obs_properties_add_path(
					group, param_name, display_name.array,
					OBS_PATH_FILE,
					shader_filter_texture_file_filter,
					NULL);
			}
			break;
		case GS_SHADER_PARAM_STRING:
			if (widget_type != NULL &&
			    strcmp(widget_type, "info") == 0) {
				obs_properties_add_text(group, param_name,
							display_name.array,
							OBS_TEXT_INFO);
			} else {
				obs_properties_add_text(group, param_name,
							display_name.array,
							OBS_TEXT_MULTILINE);
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
	filter->expand_bottom =
		(int)obs_data_get_int(settings, "expand_bottom");
	filter->use_shader_elapsed_time =
		(bool)obs_data_get_bool(settings, "use_shader_elapsed_time");
	filter->rand_activation_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);

	if (filter->reload_effect) {
		filter->reload_effect = false;
		shader_filter_reload_effect(filter);
		obs_source_update_properties(filter->context);
	}

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		//gs_eparam_t *annot = gs_param_get_annotation_by_idx(param->param, param_index);
		const char *param_name = param->name.array;
		struct dstr sources_name = {0};
		obs_source_t *source = NULL;
		void *default_value = gs_effect_get_default_val(param->param);
		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			if (default_value != NULL)
				obs_data_set_default_bool(
					settings, param_name,
					*(bool *)default_value);
			param->value.i =
				obs_data_get_bool(settings, param_name);
			break;
		case GS_SHADER_PARAM_FLOAT:
			if (default_value != NULL)
				obs_data_set_default_double(
					settings, param_name,
					*(float *)default_value);
			param->value.f =
				obs_data_get_double(settings, param_name);
			break;
		case GS_SHADER_PARAM_INT:
			if (default_value != NULL)
				obs_data_set_default_int(settings, param_name,
							 *(int *)default_value);
			param->value.i = obs_data_get_int(settings, param_name);
			break;
		case GS_SHADER_PARAM_VEC4: { // Assumed to be a color.
			struct vec4 *rgba = default_value;
			if (rgba != NULL) {
				obs_data_set_default_int(settings, param_name,
							 vec4_to_rgba(rgba));
			} else {
				// Hack to ensure we have a default...(white)
				obs_data_set_default_int(settings, param_name,
							 0xffffffff);
			}
			uint32_t c = (uint32_t)obs_data_get_int(settings,
								param_name);
			struct vec4 color;
			vec4_from_rgba_srgb(&color, c);
			param->value.i = vec4_to_rgba(&color);
			break;
		}
		case GS_SHADER_PARAM_TEXTURE:
			dstr_init_copy_dstr(&sources_name, &param->name);
			dstr_cat(&sources_name, "_source");
			const char *sn = obs_data_get_string(
				settings, sources_name.array);
			dstr_free(&sources_name);
			source = obs_weak_source_get_source(param->source);
			if (source &&
			    strcmp(obs_source_get_name(source), sn) != 0) {
				obs_source_release(source);
				source = NULL;
			}
			if (!source)
				source = (sn && strlen(sn))
						 ? obs_get_source_by_name(sn)
						 : NULL;
			if (source) {
				if (!obs_weak_source_references_source(
					    param->source, source)) {
					obs_weak_source_release(param->source);
					param->source =
						obs_source_get_weak_source(
							source);
				}
				obs_source_release(source);
				if (param->image) {
					gs_image_file_free(param->image);
					param->image = NULL;
				}
				dstr_free(&param->path);
			} else {
				const char *path = default_value;
				if (!obs_data_has_user_value(settings,
							     param_name) &&
				    path && strlen(path)) {
					if (os_file_exists(path)) {
						char *abs_path =
							os_get_abs_path_ptr(
								path);
						obs_data_set_default_string(
							settings, param_name,
							abs_path);
						bfree(abs_path);
					} else {
						struct dstr texture_path = {0};
						dstr_init(&texture_path);
						dstr_cat(
							&texture_path,
							obs_get_module_data_path(
								obs_current_module()));
						dstr_cat(&texture_path,
							 "/textures/");
						dstr_cat(&texture_path, path);
						char *abs_path =
							os_get_abs_path_ptr(
								texture_path
									.array);
						if (os_file_exists(abs_path)) {
							obs_data_set_default_string(
								settings,
								param_name,
								abs_path);
						}
						bfree(abs_path);
						dstr_free(&texture_path);
					}
				}
				path = obs_data_get_string(settings,
							   param_name);
				bool n = false;
				if (param->image == NULL) {
					param->image = bzalloc(
						sizeof(gs_image_file_t));
					n = true;
				}
				if (n || !path || !param->path.array ||
				    strcmp(path, param->path.array) != 0) {

					if (!n) {
						obs_enter_graphics();
						gs_image_file_free(
							param->image);
						obs_leave_graphics();
					}
					gs_image_file_init(param->image, path);
					dstr_copy(&param->path, path);
					obs_enter_graphics();
					gs_image_file_init_texture(
						param->image);
					obs_leave_graphics();
				}
				obs_weak_source_release(param->source);
				param->source = NULL;
			}
			break;
		case GS_SHADER_PARAM_STRING:
			if (default_value != NULL)
				obs_data_set_default_string(
					settings, param_name,
					(const char *)default_value);
			param->value.string = (char *)obs_data_get_string(
				settings, param_name);
			break;
		default:;
		}
		bfree(default_value);
	}
}

static void shader_filter_tick(void *data, float seconds)
{
	struct shader_filter_data *filter = data;
	obs_source_t *target = filter->transition
				       ? filter->context
				       : obs_filter_get_target(filter->context);
	if (!target)
		return;
	// Determine offsets from expansion values.
	int base_width = obs_source_get_base_width(target);
	int base_height = obs_source_get_base_height(target);

	filter->total_width =
		filter->expand_left + base_width + filter->expand_right;
	filter->total_height =
		filter->expand_top + base_height + filter->expand_bottom;

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
	filter->rand_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);

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

	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->context),
		OBS_COUNTOF(preferred_spaces), preferred_spaces);

	const enum gs_color_format format =
		gs_get_format_from_space(source_space);

	// Set up our input_texrender to catch the output texture.
	filter->input_texrender =
		create_or_reset_texrender(filter->input_texrender);

	// Start the rendering process with our correct color space params,
	// And set up your texrender to recieve the created texture.
	if (!filter->transition &&
	    !obs_source_process_filter_begin_with_color_space(
		    filter->context, format, source_space,
		    OBS_NO_DIRECT_RENDERING))
		return;

	if (gs_texrender_begin(filter->input_texrender, filter->total_width,
			       filter->total_height)) {

		gs_blend_state_push();
		gs_reset_blend_state();
		gs_enable_blending(false);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		gs_ortho(0.0f, (float)filter->total_width, 0.0f,
			 (float)filter->total_height, -100.0f, 100.0f);
		// The incoming source is pre-multiplied alpha, so use the
		// OBS default effect "DrawAlphaDivide" technique to convert
		// the colors back into non-pre-multiplied space. If the shader
		// file has #define USE_PM_ALPHA 1, then use normal "Draw"
		// technique.
		const char *technique =
			filter->use_pm_alpha ? "Draw" : "DrawAlphaDivide";
		if (!filter->transition)
			obs_source_process_filter_tech_end(filter->context,
							   pass_through,
							   filter->total_width,
							   filter->total_height,
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

	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->context),
		OBS_COUNTOF(preferred_spaces), preferred_spaces);

	const enum gs_color_format format =
		gs_get_format_from_space(source_space);

	if (!obs_source_process_filter_begin_with_color_space(
		    filter->context, format, source_space,
		    OBS_ALLOW_DIRECT_RENDERING)) {
		return;
	}

	gs_texture_t *texture =
		gs_texrender_get_texture(filter->output_texrender);
	gs_effect_t *pass_through = filter->output_effect;

	if (filter->param_output_image) {
		gs_effect_set_texture(filter->param_output_image, texture);
	}

	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
				   GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	obs_source_process_filter_end(filter->context, pass_through,
				      filter->total_width,
				      filter->total_height);
	gs_blend_state_pop();
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
		gs_effect_set_vec2(filter->param_uv_pixel_interval,
				   &filter->uv_pixel_interval);
	}
	if (filter->param_elapsed_time != NULL) {
		if (filter->use_shader_elapsed_time) {
			gs_effect_set_float(filter->param_elapsed_time,
					    filter->elapsed_time -
						    filter->shader_start_time);
		} else {
			gs_effect_set_float(filter->param_elapsed_time,
					    filter->elapsed_time);
		}
	}
	if (filter->param_uv_size != NULL) {
		gs_effect_set_vec2(filter->param_uv_size, &filter->uv_size);
	}
	if (filter->param_local_time != NULL) {
		gs_effect_set_float(filter->param_local_time,
				    filter->local_time);
	}
	if (filter->param_loops != NULL) {
		gs_effect_set_int(filter->param_loops, filter->loops);
	}
	if (filter->param_rand_f != NULL) {
		gs_effect_set_float(filter->param_rand_f, filter->rand_f);
	}
	if (filter->param_rand_activation_f != NULL) {
		gs_effect_set_float(filter->param_rand_activation_f,
				    filter->rand_activation_f);
	}
	if (filter->param_rand_instance_f != NULL) {
		gs_effect_set_float(filter->param_rand_instance_f,
				    filter->rand_instance_f);
	}

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		struct vec4 color;
		obs_source_t *source = NULL;

		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			gs_effect_set_bool(param->param, param->value.i);
			break;
		case GS_SHADER_PARAM_FLOAT:
			gs_effect_set_float(param->param,
					    (float)param->value.f);
			break;
		case GS_SHADER_PARAM_INT:
			gs_effect_set_int(param->param, (int)param->value.i);
			break;
		case GS_SHADER_PARAM_VEC4:
			vec4_from_rgba(&color, (unsigned int)param->value.i);
			gs_effect_set_vec4(param->param, &color);
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
					obs_source_get_color_space(
						source,
						OBS_COUNTOF(preferred_spaces),
						preferred_spaces);
				const enum gs_color_format format =
					gs_get_format_from_space(space);
				if (!param->render ||
				    gs_texrender_get_format(param->render) !=
					    format) {
					gs_texrender_destroy(param->render);
					param->render = gs_texrender_create(
						format, GS_ZS_NONE);
				} else {
					gs_texrender_reset(param->render);
				}
				uint32_t base_width =
					obs_source_get_base_width(source);
				uint32_t base_height =
					obs_source_get_base_height(source);
				gs_blend_state_push();
				gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
				if (gs_texrender_begin_with_color_space(
					    param->render, base_width,
					    base_height, space)) {
					const float w = (float)base_width;
					const float h = (float)base_height;
					uint32_t flags =
						obs_source_get_output_flags(
							source);
					const bool custom_draw =
						(flags &
						 OBS_SOURCE_CUSTOM_DRAW) != 0;
					const bool async =
						(flags & OBS_SOURCE_ASYNC) != 0;
					struct vec4 clear_color;

					vec4_zero(&clear_color);
					gs_clear(GS_CLEAR_COLOR, &clear_color,
						 0.0f, 0);
					gs_ortho(0.0f, w, 0.0f, h, -100.0f,
						 100.0f);

					if (!custom_draw && !async)
						obs_source_default_render(
							source);
					else
						obs_source_video_render(source);
					gs_texrender_end(param->render);
				}
				gs_blend_state_pop();
				obs_source_release(source);
				gs_texture_t *tex =
					gs_texrender_get_texture(param->render);
				gs_effect_set_texture(param->param, tex);
			} else if (param->image) {
				gs_effect_set_texture(param->param,
						      param->image->texture);
			} else {
				gs_effect_set_texture(param->param, NULL);
			}

			break;
		case GS_SHADER_PARAM_STRING:
			gs_effect_set_val(param->param,
					  (param->value.string
						   ? param->value.string
						   : NULL),
					  gs_effect_get_val_size(param->param));
			break;
		default:;
		}
	}
}

static void render_shader(struct shader_filter_data *filter)
{
	gs_texture_t *texture =
		gs_texrender_get_texture(filter->input_texrender);
	if (!texture) {
		return;
	}

	filter->output_texrender =
		create_or_reset_texrender(filter->output_texrender);

	if (filter->param_image) {
		gs_effect_set_texture(filter->param_image, texture);
	}
	shader_filter_set_effect_params(filter);

	gs_blend_state_push();
	gs_reset_blend_state();
	gs_enable_blending(false);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->output_texrender, filter->total_width,
			       filter->total_height)) {
		gs_ortho(0.0f, (float)filter->total_width, 0.0f,
			 (float)filter->total_height, -100.0f, 100.0f);
		while (gs_effect_loop(filter->effect, "Draw"))
			gs_draw_sprite(texture, 0, filter->total_width,
				       filter->total_height);
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
	obs_data_set_default_string(settings, "shader_text",
				    effect_template_default_image_shader);
}

static enum gs_color_space
shader_filter_get_color_space(void *data, size_t count,
			      const enum gs_color_space *preferred_spaces)
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
	return obs_source_get_color_space(target, OBS_COUNTOF(potential_spaces),
					  potential_spaces);
}

struct obs_source_info shader_filter = {
	.id = "shader_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB |
			OBS_SOURCE_CUSTOM_DRAW,
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
};

static void *shader_transition_create(obs_data_t *settings,
				      obs_source_t *source)
{
	struct shader_filter_data *filter =
		bzalloc(sizeof(struct shader_filter_data));
	filter->context = source;
	filter->reload_effect = true;
	filter->transition = true;

	dstr_init(&filter->last_path);
	dstr_copy(&filter->last_path,
		  obs_data_get_string(settings, "shader_file_name"));
	filter->last_from_file = obs_data_get_bool(settings, "from_file");
	filter->rand_instance_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);
	filter->rand_activation_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);

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

static bool shader_transition_audio_render(void *data, uint64_t *ts_out,
					   struct obs_source_audio_mix *audio,
					   uint32_t mixers, size_t channels,
					   size_t sample_rate)
{
	struct shader_filter_data *filter = data;
	return obs_transition_audio_render(filter->context, ts_out, audio,
					   mixers, channels, sample_rate, mix_a,
					   mix_b);
}

static void shader_transition_video_callback(void *data, gs_texture_t *a,
					     gs_texture_t *b, float t,
					     uint32_t cx, uint32_t cy)
{
	if (!a && !b)
		return;

	struct shader_filter_data *filter = data;
	if (filter->effect == NULL || filter->rendering)
		return;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	if (gs_get_color_space() == GS_CS_SRGB) {
		if (filter->param_image_a != NULL)
			gs_effect_set_texture(filter->param_image_a, a);
		if (filter->param_image_b != NULL)
			gs_effect_set_texture(filter->param_image_b, b);
		if (filter->param_image != NULL)
			gs_effect_set_texture(filter->param_image,
					      t < 0.5 ? a : b);
		if (filter->param_convert_linear)
			gs_effect_set_bool(filter->param_convert_linear, true);
	} else {
		if (filter->param_image_a != NULL)
			gs_effect_set_texture_srgb(filter->param_image_a, a);
		if (filter->param_image_b != NULL)
			gs_effect_set_texture_srgb(filter->param_image_b, b);
		if (filter->param_image != NULL)
			gs_effect_set_texture_srgb(filter->param_image,
						   t < 0.5 ? a : b);
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
	obs_transition_video_render2(filter->context,
				     shader_transition_video_callback, NULL);

	gs_set_linear_srgb(previous);
}

static enum gs_color_space
shader_transition_get_color_space(void *data, size_t count,
				  const enum gs_color_space *preferred_spaces)
{
	struct shader_filter_data *filter = data;
	const enum gs_color_space transition_space =
		obs_transition_video_get_color_space(filter->context);

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
	.get_defaults = shader_filter_defaults,
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
