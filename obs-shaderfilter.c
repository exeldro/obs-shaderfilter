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

/* clang-format off */

#define nullptr ((void*)0)

static const char *effect_template_begin =
"\r\
uniform float4x4 ViewProj;\r\
uniform texture2d image;\r\
\r\
uniform float2 uv_offset;\r\
uniform float2 uv_scale;\r\
uniform float2 uv_pixel_interval;\r\
uniform float2 uv_size;\r\
uniform float rand_f;\r\
uniform float rand_instance_f;\r\
uniform float rand_activation_f;\r\
uniform float elapsed_time;\r\
uniform int loops;\r\
uniform float local_time;\r\
\r\
sampler_state textureSampler{\r\
	Filter = Linear;\r\
	AddressU = Border;\r\
	AddressV = Border;\r\
	BorderColor = 00000000;\r\
};\r\
\r\
struct VertData {\r\
	float4 pos : POSITION;\r\
	float2 uv : TEXCOORD0;\r\
};\r\
\r\
VertData mainTransform(VertData v_in)\r\
{\r\
	VertData vert_out;\r\
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);\r\
	vert_out.uv = v_in.uv * uv_scale + uv_offset;\r\
	return vert_out;\r\
}\r\
\r\
";

static const char *effect_template_default_image_shader =
"\r\
float4 mainImage(VertData v_in) : TARGET\r\
{\r\
	return image.Sample(textureSampler, v_in.uv);\r\
}\r\
";

static const char *effect_template_end =
"\r\
technique Draw\r\
{\r\
	pass\r\
	{\r\
		vertex_shader = mainTransform(v_in);\r\
		pixel_shader = mainImage(v_in);\r\
	}\r\
}";

/* clang-format on */

struct effect_param_data {
	struct dstr name;
	struct dstr display_name;
	struct dstr widget_type;
	struct dstr description;
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

	bool reload_effect;
	struct dstr last_path;
	bool last_from_file;

	//uint64_t shader_last_time;
	float shader_start_time;
	//uint64_t shader_duration;

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

	int expand_left;
	int expand_right;
	int expand_top;
	int expand_bottom;

	int total_width;
	int total_height;
	bool use_sliders;
	bool use_sources; //consider using name instead, "source_name" or use annotation
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
	char *file = bstrdup(os_quick_read_utf8_file(file_name));
	char **lines = strlist_split(file, '\n', true);
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
	bfree(file);
	strlist_free(lines);
	return shader_file.array;
}

static void shader_filter_clear_params(struct shader_filter_data *filter)
{
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
		da_free(param->option_values);
		for (size_t i = 0; i < param->option_labels.num; i++) {
			dstr_free(&param->option_labels.array[i]);
		}
		da_free(param->option_labels);
	}

	da_free(filter->stored_param_list);
}

static void shader_filter_reload_effect(struct shader_filter_data *filter)
{
	obs_data_t *settings = obs_source_get_settings(filter->context);

	// First, clean up the old effect and all references to it.
	filter->shader_start_time = 0.0;
	shader_filter_clear_params(filter);

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
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	filter->effect = gs_effect_create(effect_text.array, NULL, &errors);
	obs_leave_graphics();

	dstr_free(&effect_text);

	if (filter->effect == NULL) {
		blog(LOG_WARNING,
		     "[obs-shaderfilter] Unable to create effect. Errors returned from parser:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)"
							    : errors));
		bfree(errors);
		goto end;
	}

	// Store references to the new effect's parameters.
	da_init(filter->stored_param_list);

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
		} else if (strcmp(info.name, "ViewProj") == 0 ||
			   strcmp(info.name, "image") == 0) {
			// Nothing.
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
				gs_effect_get_param_info(annotation, &info);
				if (strcmp(info.name, "name") == 0 &&
				    info.type == GS_SHADER_PARAM_STRING) {
					dstr_copy(
						&cached_data->display_name,
						(const char *)
							gs_effect_get_default_val(
								annotation));
				} else if (strcmp(info.name, "label") == 0 &&
					   info.type ==
						   GS_SHADER_PARAM_STRING) {
					dstr_copy(
						&cached_data->display_name,
						(const char *)
							gs_effect_get_default_val(
								annotation));
				} else if (strcmp(info.name, "widget_type") ==
						   0 &&
					   info.type ==
						   GS_SHADER_PARAM_STRING) {
					dstr_copy(
						&cached_data->widget_type,
						(const char *)
							gs_effect_get_default_val(
								annotation));
				} else if (strcmp(info.name, "minimum") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->minimum.f =
							*(float *)gs_effect_get_default_val(
								annotation);
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->minimum.i =
							*(int *)gs_effect_get_default_val(
								annotation);
					}
				} else if (strcmp(info.name, "maximum") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->maximum.f =
							*(float *)gs_effect_get_default_val(
								annotation);
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->maximum.i =
							*(int *)gs_effect_get_default_val(
								annotation);
					}
				} else if (strcmp(info.name, "step") == 0) {
					if (info.type ==
					    GS_SHADER_PARAM_FLOAT) {
						cached_data->step.f =
							*(float *)gs_effect_get_default_val(
								annotation);
					} else if (info.type ==
						   GS_SHADER_PARAM_INT) {
						cached_data->step.i =
							*(int *)gs_effect_get_default_val(
								annotation);
					}
				} else if (strncmp(info.name, "option_", 7) ==
					   0) {
					int id = atoi(info.name + 7);
					if (info.type == GS_SHADER_PARAM_INT) {
						int val =
							*(int *)gs_effect_get_default_val(
								annotation);
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
							(const char *)gs_effect_get_default_val(
								annotation));
						struct dstr *cs = da_insert_new(
							cached_data
								->option_labels,
							id);
						*cs = val;
					}
				}
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
	filter->use_sliders = obs_data_get_bool(settings, "use_sliders");
	filter->rand_instance_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);
	filter->rand_activation_f =
		(float)((double)rand_interval(0, 10000) / (double)10000);

	da_init(filter->stored_param_list);

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
	}

	return false;
}

static bool use_sliders_changed(obs_properties_t *props, obs_property_t *p,
				obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	struct shader_filter_data *filter = obs_properties_get_param(props);

	bool use_sliders = obs_data_get_bool(settings, "use_sliders");
	if (use_sliders != filter->use_sliders) {
		filter->reload_effect = true;
	}
	filter->use_sliders = use_sliders;

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
	obs_property_list_add_string(p, name, name);
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

	obs_properties_add_int(props, "expand_left",
			       obs_module_text("ShaderFilter.ExpandLeft"), 0,
			       9999, 1);
	obs_properties_add_int(props, "expand_right",
			       obs_module_text("ShaderFilter.ExpandRight"), 0,
			       9999, 1);
	obs_properties_add_int(props, "expand_top",
			       obs_module_text("ShaderFilter.ExpandTop"), 0,
			       9999, 1);
	obs_properties_add_int(props, "expand_bottom",
			       obs_module_text("ShaderFilter.ExpandBottom"), 0,
			       9999, 1);

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

	obs_property_t *use_sliders = obs_properties_add_bool(
		props, "use_sliders",
		obs_module_text("ShaderFilter.UseSliders"));
	obs_property_set_modified_callback(use_sliders, use_sliders_changed);

	obs_property_t *use_shader_elapsed_time = obs_properties_add_bool(
		props, "use_shader_elapsed_time",
		obs_module_text("ShaderFilter.UseShaderElapsedTime"));
	obs_property_set_modified_callback(use_shader_elapsed_time,
					   use_shader_elapsed_time_changed);

	obs_properties_add_button(props, "reload_effect",
				  obs_module_text("ShaderFilter.ReloadEffect"),
				  shader_filter_reload_effect_clicked);

	size_t param_count = filter->stored_param_list.num;
	for (size_t param_index = 0; param_index < param_count; param_index++) {
		struct effect_param_data *param =
			(filter->stored_param_list.array + param_index);
		//gs_eparam_t *annot = gs_param_get_annotation_by_idx(param->param, param_index);
		const char *param_name = param->name.array;
		const char *label = param->display_name.array;
		const char *widget_type = param->widget_type.array;
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

		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			obs_properties_add_bool(props, param_name,
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
					props, param_name, display_name.array,
					range_min, range_max, step);
			} else {
				obs_properties_add_float(props, param_name,
							 display_name.array,
							 -FLT_MAX, FLT_MAX,
							 0.000001);
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
					props, param_name, display_name.array,
					range_min, range_max, step);
			} else if (widget_type != NULL &&
				   strcmp(widget_type, "select") == 0) {
				obs_property_t *plist = obs_properties_add_list(
					props, param_name, display_name.array,
					OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);
				for (size_t i = 0; i < param->option_values.num;
				     i++) {
					obs_property_list_add_int(
						plist, option_labels[i].array,
						options[i]);
				}
			} else {
				obs_properties_add_int(props, param_name,
						       display_name.array,
						       INT_MIN, INT_MAX, 1);
			}
			break;
		}
		case GS_SHADER_PARAM_INT3:

			break;
		case GS_SHADER_PARAM_VEC4:
			obs_properties_add_color_alpha(props, param_name,
						       display_name.array);
			break;
		case GS_SHADER_PARAM_TEXTURE:
			dstr_init_copy_dstr(&sources_name, &param->name);
			dstr_cat(&sources_name, "_source");
			obs_property_t *p = obs_properties_add_list(
				props, sources_name.array, display_name.array,
				OBS_COMBO_TYPE_EDITABLE,
				OBS_COMBO_FORMAT_STRING);
			dstr_free(&sources_name);
			obs_property_list_add_string(p, "", "");
			obs_enum_sources(add_source_to_list, p);
			obs_enum_scenes(add_source_to_list, p);
			obs_properties_add_path(
				props, param_name, display_name.array,
				OBS_PATH_FILE,
				shader_filter_texture_file_filter, NULL);
			break;
		case GS_SHADER_PARAM_STRING:
			obs_properties_add_text(props, param_name,
						display_name.array,
						OBS_TEXT_MULTILINE);
			break;
		default:;
		}
		dstr_free(&display_name);
	}

	dstr_free(&examples_path);
	UNUSED_PARAMETER(data);
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
	filter->use_sliders = (bool)obs_data_get_bool(settings, "use_sliders");
	filter->use_sources = (bool)obs_data_get_bool(settings, "use_sources");
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
		struct dstr display_name = {0};
		struct dstr sources_name = {0};
		obs_source_t *source = NULL;
		dstr_ncat(&display_name, param_name, param->name.len);
		dstr_replace(&display_name, "_", " ");

		switch (param->type) {
		case GS_SHADER_PARAM_BOOL:
			if (gs_effect_get_default_val(param->param) != NULL)
				obs_data_set_default_bool(
					settings, param_name,
					*(bool *)gs_effect_get_default_val(
						param->param));
			param->value.i =
				obs_data_get_bool(settings, param_name);
			break;
		case GS_SHADER_PARAM_FLOAT:
			if (gs_effect_get_default_val(param->param) != NULL)
				obs_data_set_default_double(
					settings, param_name,
					*(float *)gs_effect_get_default_val(
						param->param));
			param->value.f =
				obs_data_get_double(settings, param_name);
			break;
		case GS_SHADER_PARAM_INT:
			if (gs_effect_get_default_val(param->param) != NULL)
				obs_data_set_default_int(
					settings, param_name,
					*(int *)gs_effect_get_default_val(
						param->param));
			param->value.i = obs_data_get_int(settings, param_name);
			break;
		case GS_SHADER_PARAM_VEC4: // Assumed to be a color.
			if (gs_effect_get_default_val(param->param) != NULL) {
				obs_data_set_default_int(
					settings, param_name,
					*(unsigned int *)
						gs_effect_get_default_val(
							param->param));
			} else {
				// Hack to ensure we have a default...(white)
				obs_data_set_default_int(settings, param_name,
							 0xffffffff);
			}
			param->value.i = obs_data_get_int(settings, param_name);
			break;
		case GS_SHADER_PARAM_TEXTURE:
			dstr_init_copy_dstr(&sources_name, &param->name);
			dstr_cat(&sources_name, "_source");
			const char *sn = obs_data_get_string(
				settings, sources_name.array);
			dstr_free(&sources_name);
			source = (sn && strlen(sn)) ? obs_get_source_by_name(sn)
						    : NULL;
			if (source) {
				obs_weak_source_release(param->source);
				param->source =
					obs_source_get_weak_source(source);
				obs_source_release(source);
				if (param->image) {
					gs_image_file_free(param->image);
					param->image = NULL;
				}
			} else {
				if (param->image == NULL) {
					param->image = bzalloc(
						sizeof(gs_image_file_t));
				} else {
					obs_enter_graphics();
					gs_image_file_free(param->image);
					obs_leave_graphics();
				}

				gs_image_file_init(
					param->image,
					obs_data_get_string(settings,
							    param_name));

				obs_enter_graphics();
				gs_image_file_init_texture(param->image);
				obs_leave_graphics();
				obs_weak_source_release(param->source);
				param->source = NULL;
			}
			break;
		case GS_SHADER_PARAM_STRING:
			if (gs_effect_get_default_val(param->param) != NULL)
				obs_data_set_default_string(
					settings, param_name,
					(const char *)gs_effect_get_default_val(
						param->param));
			param->value.string = (char *)obs_data_get_string(
				settings, param_name);
			break;
		default:;
		}
	}
}

static void shader_filter_tick(void *data, float seconds)
{
	struct shader_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);
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
}

static void shader_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct shader_filter_data *filter = data;

	if (filter->effect == NULL || filter->rendering) {
		obs_source_skip_video_filter(filter->context);
		return;
	}
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
					     OBS_NO_DIRECT_RENDERING)) {
		return;
	}
	filter->rendering = true;
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
		//void *defvalue = gs_effect_get_default_val(param->param);
		//float tempfloat;

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

	obs_source_process_filter_end(filter->context, filter->effect,
				      filter->total_width,
				      filter->total_height);
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

struct obs_source_info shader_filter = {.id = "shader_filter",
					.type = OBS_SOURCE_TYPE_FILTER,
					.output_flags = OBS_SOURCE_VIDEO,
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
					.get_properties =
						shader_filter_properties};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-shaderfilter", "en-US")

bool obs_module_load(void)
{
	obs_register_source(&shader_filter);

	return true;
}

void obs_module_unload(void) {}
