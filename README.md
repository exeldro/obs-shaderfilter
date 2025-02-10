# obs-shaderfilter

## Introduction

The obs-shaderfilter plugin for [OBS Studio](http://obsproject.com/) is intended to allow users to apply 
their own shaders to OBS sources. This theoretically makes possible some simple effects like drop shadows
that can be implemented strictly in shader code. 

Please note that this plugin may expose a reasonable number of bugs in OBS, as it uses the shader parser and 
the property system in somewhat unusual ways.

## Installation

The binary package mirrors the structure of the OBS Studio installation directory, so you should be able to
just drop its contents alongside an OBS Studio install (usually at C:\Program Files\obs-studio\). The 
necessary files should look like this: 

    obs-studio
    |---data
    |   |---obs-plugins
    |       |---obs-shaderfilter
    |           |---examples
    |               |---blink.shader
    |               |---border.shader
    |               |---drop_shadow.shader
    |               |---filter-template.effect
    |               |---multiply.shader
    |               |---pulse.effect
    |               |---rectangular_drop_shadow.shader
    |               |---rounded_rect.shader
    |               |---many more...
    |           |---locale
    |               |---en-US.ini
    |---obs-plugins
        |---32bit
        |   |---obs-shaderfilter.dll
        |---64bit
            |---obs-shaderfilter.dll

## Usage

The filter can be added to any source through the "Filters" option when right-clicking on a source. The name
of the filter is "User-defined shader." 

Shaders can either be entered directly in a text box in the filter properties, or loaded from a file. To change 
between the two modes, use the "Load shader text from file" toggle. If you are entering your shader text directly,
note that you will need to use the "Reload effect" button to apply your changes. This can also be used to reload an external file if changes have been made. 
OBS shaders are written in OBS version of HLSL.

The option is provided to render extra pixels on each side of the source. This is useful for effects like shadows
that need to render outside the bounds of the original source. 

Normally, all that's required for OBS purposes is a pixel shader, so the plugin will wrap your shader text with a 
standard template to add a basic vertex shader and other boilerplate. If you wish to customize the vertex shader
or other parts of the effect for some reason, you can check the "Use Effect File (.effect)" option. 

Any parameters you add to your shader (defined as `uniform` variables) will be detected by the plugin and exposed
in the properties window to have their values set. Currently, only `int`, `float`, `bool`, `string`, `texture2d`, and `float4`
parameters are supported. (`float4` parameters will be interpreted by the properties window as colors.) `string` is used for 
notes and instructions, but could be used in an effect or shader. Variable names are displayed in the GUI with underscore replaced with space `uniform float Variable_Name` becomes `Variable Name`.

Version 2.0 and up support setting label, widget_type, minimum, maximum, step using annotations.
Version 2.1 and up support setting group using annotations.
A slider that has a minimum of -1.0, maximum of 1.0, and a step size of 0.01:
```
// Contrast from -1.0 to 1.0
uniform float Contrast<
  string label = "Contrast Adjustment";
  string widget_type = "slider";
  string group = "Group";
  float minimum = -1.0;
  float maximum = 1.0;
  float step = 0.01;
> = 0.0;
```
A drop-down select widget for integer fields:
```
uniform int SelectTest<
  string label = "Int Select";
  string widget_type = "select";
  int    option_0_value = 0;
  string option_0_label = "First";
  int    option_1_value = 1;
  string option_1_label = "Second";
  int    option_2_value = 3;
  string option_2_label = "Third";
> = 3;
```
A text field the user can not edit:
```
uniform string notes<
    string widget_type = "info";
> = "add notes here";
```

#### Defaults

You set default values as a normal assignment ```uniform string notes = 'my note';```, except for `float4` 
which requires bracket \{\} notation like ```uniform float4 mycolor = { 0.75, 0.75, 0.75, 1.0};``` 

Note that if your shader has syntax errors and fails to compile, OBS does not provide any error messages; you will
simply see your source render nothing at all. In many cases the output of the effect parser will be written to the
OBS log file, which you can view with the Help -> Log Files menu in OBS. 

### Standard parameters

The plugin automatically populates a few parameters which your shader can use. If you choose to override the entire
effect, be sure to define these as `uniform` variables and use them where necessary. (The filter should gracefully 
handle these variables being missing, but the shader may malfunction.)

* **`ViewProj`** (`float4x4`)&mdash;The view/projection matrix. (Standard for all OBS filters.)
* **`image`** (`texture2d`)&mdash;The image to which the filter is being applied, either the original output of 
  the source or the output of the previous filter in the chain. (Standard for all OBS filters.)
* **`elapsed_time`** (`float`)&mdash;The time in seconds which has elapsed since the filter was created. Useful for 
  creating animations.
* **`elapsed_time_start`** (`float`)&mdash;The time in seconds which has elapsed since the shader was loaded (2.4.0).
* **`elapsed_time_show`** (`float`)&mdash;The time in seconds which has elapsed since the filter was shown (2.4.0).
* **`elapsed_time_active`** (`float`)&mdash;The time in seconds which has elapsed since the filter has become active (2.4.0).
* **`elapsed_time_enable`** (`float`)&mdash;The time in seconds which has elapsed since the filter was enabled (2.4.0).
* **`local_time`** (`float`)&mdash; a random float representing the local time.(1.2)
* **`loops`** (`int`)&mdash; count of how many seconds the shader has rendered a page.(1.2)
* **`loop_second`** (`float`)&mdash; a float going from 0.0 to 1.0 each loop.(2.4.0)
* **`current_time_ms`** (`int`)&mdash; the microseconds (0 to 999) of the current time.(2.4.1)
* **`current_time_sec`** (`int`)&mdash; the seconds (0 to 59) of the current time.(2.4.1)
* **`current_time_min`** (`int`)&mdash; the minutes (0 to 59) of the current time.(2.4.1)
* **`current_time_hour`** (`int`)&mdash; the hours (0 to 23) of the current time.(2.4.1)
* **`current_time_day_of_week`** (`int`)&mdash; the day of the week (0 to 6) of the current time.(2.4.1)
* **`current_time_day_of_month`** (`int`)&mdash; the day of the month (1 to 31) of the current time.(2.4.1)
* **`current_time_month`** (`int`)&mdash; the month (0 to 11) of the current time.(2.4.1)
* **`current_time_day_of_year`** (`int`)&mdash; the day of the month (0 to 365) of the current time.(2.4.1)
* **`current_time_year`** (`int`)&mdash; the number of years since 1900 of the current time.(2.4.1)
* **`rand_f`** (`float`)&mdash; a random float between 0 and 1. changes per frame.
* **`rand_activation_f`** (`float`)&mdash; a random float between 0 and 1. changes per activation, load or change of settings.(1.2)
* **`rand_instance_f`** (`float`)&mdash; a random float between 0 and 1. changes per instance on load.(1.2)
* **`uv_offset`** (`float2`)&mdash;The offset which should be applied to the UV coordinates of the vertices. This is
  used in the standard vertex shader to draw extra pixels on the borders of the source.
* **`uv_scale`** (`float2`)&mdash;The scale which should be applied to the UV coordinates of the vertices. This is 
  used in the standard vertex shader to draw extra pixels on the borders of the source.
* **`uv_size`** (`float2`)&mdash;The height and width of the screen.
* **`uv_pixel_interval`** (`float2`)&mdash;This is the size in UV coordinates of an individual texel. You can use
  this to convert the UV coordinates of the pixel being processed to the coordinates of that texel in the source
  texture, or otherwise scale UV coordinate distances into texel distances.

### Optional Preprocessing Macros

The plugin provides some optional pre-processing macros.

* **`#include "<path-to-file>"`** The include macro will insert the contents file at the path `<path-to-file>` before the shader is compiled. This is useful to place commonly used functions, in a separate file that can be used by multiple shaders.  E.g.: `#include "util-fns.effect"`.
* **`#define <NAME> <value>`** This allows you to define constants to be used throughout your shader. Constants can be values or even simple functions. Anywhere the value in `<NAME>` is found in your shader, it will be replaced with whatever is in `<value>`.  For example, after putting `#define PI 3.14159` near the top of your shader file, you can use code like: `float circle_area = PI * radius * radius;`.  Note, the `#define` line should NOT be ended with a semicolon.
* **`#define USE_PM_ALPHA 1`** By default, OBS will pass through pre-multiplied alpha color values. This can cause issues if the source being filtered has opacity values that are not zero or one. By default, shaderfilter now corrects internally for premultipled alpha, but if you have written an older shader that does the correction itself, you can turn off the correction by placing `#define USE_PM_ALPHA 1` near the top of your shader file.

### Example shaders

Several examples are provided in the plugin's *data/examples* folder. These can be used as-is for some hopefully
useful common tasks, or used as a reference in developing your own shaders. Note that the *.shader* and *.effect* 
extensions are for clarity only, and have no specific meaning to the plugin. Text files with any extension can be
loaded. In a standard, *.effect* files include a vertex shader and *.shader* only has a pixel shader.

I recommend *.shader* as they do not require `Use Effect File (.effect)` as pixel shaders, while *.effect* signifies vertex shaders with `Use Effect File (.effect)` required.
| File                            | Description                                                                                                                                                                                                                                                                                                                                                                                             | Example                                                                                                   |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| animated_texture.effect         | Animates a texture with polar sizing and color options                                                                                                                                                                                                                                                                                                                                                  |                                                                                                           |
| alpha-gaming-bent-camera.shader |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/5fb6fec8-fc1b-46eb-96aa-17ce37a7ca20) |
| ascii.shader                    | a little example of ascii art                                                                                                                                                                                                                                                                                                                                                                           | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/682ad2d3-d32a-464e-a3af-0791ba0fc829) |
| background_removal.effect       | simple implementation of background removal. Optional color space corrections                                                                                                                                                                                                                                                                                                                           |                                                                                                           |
| blink.shader                    | A shader that fades the opacity of the output in and out over time, with a configurable speed multiplier. Demonstrates the user of the `elapsed_time` parameter.                                                                                                                                                                                                                                        |                                                                                                           |
| bloom.shader                    | simple shaders to add bloom effects                                                                                                                                                                                                                                                                                                                                                                     | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/567e5dc4-ec20-42fa-a344-2be1e6516b01) |
| border.shader                   | A shader that adds a solid border to all extra pixels outside the bounds of the input.                                                                                                                                                                                                                                                                                                                  |                                                                                                           |
| box-blur.shader                 |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/076efaca-c8fa-4e08-8906-46fde354dbb8) |
| burn.shader                     |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/8d703243-2db3-4fc1-8c2f-836d61fcee13) |
| cartoon.effect                  | Simple Cartooning based on hue and steps of detail value.                                                                                                                                                                                                                                                                                                                                               | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/17d272f0-b692-4452-b982-522a98d7a1f5) |
| cell_shaded.shader              |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/d07aa2ef-05fc-407f-82ab-448f0aca8730) |
| Chroma+UV-Distortion.shader     |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/c0db01c3-1e87-450c-9b4f-56a6e61a07b1) |
| chromatic-aberration.shader     |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/ab99dc36-b9c2-405d-b9ca-3216866003fa) |
| drop_shadow.shader              | A shader that adds a basic drop shadow to the input. Note that this is done with a simple uniform blur, so it won't look quite as good as a proper Gaussian blur. This is also an O(N&sup2;) blur on the size of the blur, so be very conscious of your GPU usage with a large blur size.                                                                                                               |                                                                                                           |
| dynamic-mask.shader             |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/60bac8ea-f5be-4122-8ac7-542faf051c9f) |
| edge_detection.shader           | A shader that detects edges of color. Includes support for alpha channels.                                                                                                                                                                                                                                                                                                                              |                                                                                                           |
| filter_template.effect          | A copy of the default effect used by the plugin, which simply renders the input directly to the output after scaling UVs to reflect any extra border pixels. This is useful as a starting point for developing new effects, especially those that might need a custom vertex shader. (Note that modifying this file will  not affect the internal effect template used by the plugin.)                  |                                                                                                           |
| filter_template.shader          | A copy of the default shader used by the plugin, which simply renders the input directly to the output after scaling UVs to reflect any extra border pixels. This is useful as a starting point for developing new pixel shaders. (Note that modifying this file will not affect the internal effect template used by the plugin.)                                                                      |                                                                                                           |
| fire.shader                     | A fire example converted from shadertoy with lots of added options.                                                                                                                                                                                                                                                                                                                                     | [youtube example](https://youtu.be/jcTsC0zSNAs)                                                           |
| glow.shader                     | simple shaders to add glow effects, with some additional options for animation                                                                                                                                                                                                                                                                                                                          |                                                                                                           |
| glitch_analog.shader            | A shader that creates glitch effects similar to analog signal issues. Includes support for alpha channel.                                                                                                                                                                                                                                                                                               |                                                                                                           |
| gb-camera.shader                |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/b8b85e0c-918b-4fd4-bc0c-6161569b6610) |
| gradient.shader                 | This shader has a little brother *simple_gradient.shader*, but lets you choose three colors and animate gradients.                                                                                                                                                                                                                                                                                      |                                                                                                           |
| halftone.shader                 |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/e7a555ec-ae7c-4580-b9d2-14fde0b4992d) |
| hexagon.shader                  | A shader that creates a grid of hexagons with several options for you to set. This is an example of making shapes.                                                                                                                                                                                                                                                                                      |                                                                                                           |
| intensity-scope.shader          |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/c4fd64ab-64d1-42b0-9929-ba1e3d7a6466) |
| gaussian-simple.shader          | A simple gaussian shader for bluring. Really implements closer to a box shader.                                                                                                                                                                                                                                                                                                                         |                                                                                                           |
| luminance.shader                | A shader that adds an alpha layer based on brightness instead of color. Extremely useful for making live video special effects, like replacing backgrounds or foregrounds.                                                                                                                                                                                                                              |                                                                                                           |
| matrix.effect                   | The cat is a glitch conversion from shadertoy. Updated with several configurable options.(1.2)                                                                                                                                                                                                                                                                                                          | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/79d2b028-4ea8-405d-a560-846f3ea78357) |
| multiply.shader                 | A shader that multiplies the input by another image specified in the parameters. Demonstrates the use of user-defined `texture2d` parameters.                                                                                                                                                                                                                                                           |                                                                                                           |
| night_sky.shader                | Animated moon, clouds, stars background(1.2)                                                                                                                                                                                                                                                                                                                                                            | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/8202deaa-7b60-417f-abd6-30c97b617b14) |
| page-peel.shader                |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/9ebb8c1a-4a6d-4425-a344-36dd05533db2) |
| perlin_noise.effect             | An effect generates perlin_noise, used to make water, clouds and glitch effects.                                                                                                                                                                                                                                                                                                                        | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/62f3c4eb-74b5-4dbf-9e6e-e726c8734861) |
| pie-chart.shader                |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/bf44b9aa-23e5-4261-a0f8-b287d3012db7) |
| pixelation.shader               |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/88b9db62-9fc7-4a1a-b7a2-cf22355be390) |
| pulse.effect                    | An effect that varies the size of the output over time. This demonstrates a custom vertex shader that manipulates the position of the rendered vertices based on user data. Note that moving the vertices in the vertex shader will not affect the logical size of the source in OBS, and this may mean that pixels outside the source's  bounds will get cut off by later filters in the filter chain. |                                                                                                           |
| rainbow.shader                  | Creates Rainbow effects, animated, rotating, horizontal or vertical. This is an expensive process and limiters are implemented.                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/3aa68f8f-ea24-4e9d-a813-1769d9aaf507) |
| rain-window.shader              |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/a5c60cfc-e98e-41cd-9e9a-e10367d7e1e0) |
| rectangular_drop_shadow.shader  | A shader that renders an optimized drop shadow for sources that are opaque and rectangular. Pixels inside the bounds of the input are treated as solid; pixels outside are treated as opaque. The complexity of the blur does not increase with its size, so you should be able to make your blur size as large as you like wtihout affecting GPU load.                                                 |                                                                                                           |
| remove_partial_pixels.shader    | A shader that removes pixels with partial alpha, excellent for cleaning up green screens.                                                                                                                                                                                                                                                                                                               |                                                                                                           |
| repeat.effect                   | Duplicates the input video as many times as you like and organizes on the screen.                                                                                                                                                                                                                                                                                                                       | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/19ba6e94-af2a-472a-954d-3d9bdebde99e) |
| repeat_texture.effect           | As above, but add a texture input to repeat instead of the input source.                                                                                                                                                                                                                                                                                                                                | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/08b3d442-7705-4674-9e70-12b727150947) |
| rgb_color_wheel.shader          | A rotatable RGB color wheel!                                                                                                                                                                                                                                                                                                                                                                            | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/b087379c-7aca-40b1-9f2d-d55343b7eb4d) |
| rgb_split.shader                |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/ddca6372-1ee6-495a-be52-4b6cc27be89e) |
| rotatoe.effect                  | A test rotation effect                                                                                                                                                                                                                                                                                                                                                                                  |                                                                                                           |
| rounded_rect.shader             | A shader that rounds the corners of the input, optionally adding a border outside the rounded edges.                                                                                                                                                                                                                                                                                                    |                                                                                                           |
| rounded_stroke.shader           | A shader that rounds the corners of the input, optionally adding a border outside the rounded edges. Updated by Exeldro. Several shaders have been upgraded with Apply To Specific Color for you to animate borders.                                                                                                                                                                                    | [https://youtu.be/J8mQIEKvWt0](https://youtu.be/J8mQIEKvWt0)                                              |
| scan_line.shader                | An effect that creates old style tv scan lines, for glitch style effects.                                                                                                                                                                                                                                                                                                                               | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/d5913e00-ff88-4276-8b12-e13305e5c2bd) |
| seasick.shader                  |                                                                                                                                                                                                                                                                                                                                                                                                         |                                                                                                           |
| selective_color.shader          | Create black and white effects with some colorization. (defaults: .4,.03,.25,.25, 5.0, true,true, true, true. cuttoff higher = less color, 0 = all 1 = none)                                                                                                                                                                                                                                            | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/868780e9-aa88-4546-a1cb-d7fcc23a18ca) |
| shake.effect                    | creates random screen glitch style shake. Keep the random_scale low for small (0.2-1) for small  jerky movements and larger for less often big jumps.                                                                                                                                                                                                                                                   |                                                                                                           |
| shine.shader                    | Add shine / glow to any element, use the transition luma wipes (obs-studio\plugins\obs-transitions\data\luma_wipes *SOME NEW WIPES INCLUDED IN THIS RELEASE ZIP*) or create your own, also includes a glitch (using rand_f), hide/reveal, reverse and ease, start adjustment and stop adjustment                                                                                                        |                                                                                                           |
| smart_denoise.shader            | A shader that denoise your noisy video input, based on the GLSL implementation in [BrutPitt/glslSmartDeNoise](https://github.com/BrutPitt/glslSmartDeNoise)                                                                                                                                                                                                                                             |                                                                                                           |
| spotlight.shader                | Creates a stationary or animated spotlight effect with color options, speed of animation and glitch                                                                                                                                                                                                                                                                                                     | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/f9aebc02-4da5-4d30-b2f3-a9d9d7511f9f) |
| Swirl.shader                    |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/722bc6fc-7c75-455f-a3bd-94607e621e74) |
| thermal.shader                  |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/f12e1040-51a6-4401-a84b-160f19c8f907) |
| tv-crt-subpixel.shader          |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/38d06b91-e75b-4d76-82ee-e116d306a247) |
| twist.shader                    |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/6998bbee-8a44-4a5d-82b4-ca92f246e826) |
| VCR.shader                      |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/8e4b5663-2135-4a4e-b912-6e58a6c29628) |
| VHS.shader                      |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/c61ca22f-c62c-4b38-9c28-555e57e3951b) |
| vignetting.shader               | A shader that reduces opacity further from the center of the image. inner radius is the start and outer radius is the end. suggested default settings is opacity 0.5, innerRadius = 0.5, outerRadius = 1.2                                                                                                                                                                                              | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/b7d32bb9-014d-4152-9be1-8bdb498121f0) |
| voronoi-pixelation.shader       |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/102317d5-ba5f-48bf-8666-c1d2b5850813) |
| ZigZag.shader                   |                                                                                                                                                                                                                                                                                                                                                                                                         | ![image](https://github.com/exeldro/obs-shaderfilter/assets/5457024/3e3006cd-9f2a-4a7e-905c-50d80173e861) |
| zoom_blur.shader                | A shader that creates a zoom with blur effect based on a number of samples and magnitude of each sample. It also includes an animation with or without easing and a glitch option. Set speed to zero to not use animation. Suggested values are 15 samples and 30-50 magnitude.                                                                                                                         |                                                                                                           |


## Building

If you wish to build the obs-shaderfilter plugin from source, you should just need [CMake](https://cmake.org/) 
and the OBS Studio libraries and headers.

* [obs-shaderfilter source repository](https://github.com/exeldro/obs-shaderfilter)
* [OBS Studio source repository](https://github.com/obsproject/obs-studio)

1. In-tree build
    - Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
    - Check out this repository to plugins/obs-shaderfilter
    - Add `add_subdirectory(obs-shaderfilter)` to plugins/CMakeLists.txt
    - Rebuild OBS Studio

1. Stand-alone build (Linux only)
    - Verify that you have package with development files for OBS
    - Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`

## Donations
https://www.paypal.me/exeldro
