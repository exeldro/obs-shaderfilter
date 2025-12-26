uniform string displacement_info<
  string label = "Displacement";
  string widget_type = "info";
> = "Displaces the Background Layer with the current image. Red channel is affected to X (horizontal) displacement and Green channel to Y (vertical). rgb(.5, .5, .5) is no displacement. You can choose the curve mapping to map values between -1 to 1 differently.";

uniform float displacement_x<
  string label = "Displacement X (px)";
> = 16.0;

uniform float displacement_y<
  string label = "Displacement Y (px)";
> = 16.0;

uniform int displacement_curve<
  string label = "Displacement Curve";
  string widget_type = "select";
  int    option_0_value = 1;
  string option_0_label = "Linear";
  int    option_1_value = 2;
  string option_1_label = "Quadratic";
  int    option_2_value = 3;
  string option_2_label = "Cubic";
> = 0;

uniform string blur_info<
  string label = "Blur";
  string widget_type = "info";
> = "Imitates how light disperses through displacement. It can recreate refractions like effects. Blur size affects how much light disperses, quality is the number of samples, directions of those samples (around a circle). You can choose the starting angle (use directions 1 or 2 to have highly directional refractions).";

uniform float blur_size<
    string label = "Blur Size (px)";
> = 8.0; // BLUR SIZE (Radius)
uniform float blur_quality<
  string label = "Blur Quality (4.0)";
  string widget_type = "slider";
  float minimum = 1.0;
  float maximum = 16;
  float step = 1.0;
> = 4.0; // BLUR QUALITY (Default 4.0 - More is better but slower)
uniform float blur_directions<
  string label = "Blur Directions (16.0)";
  string widget_type = "slider";
  float minimum = 1.0;
  float maximum = 24;
  float step = 1.0;
> = 16.0; // BLUR DIRECTIONS (number of rays in sampling)
uniform float blur_angle<
    string label = "Blur Angle (degrees)";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 360;
    float step = 1.0;
> = 0; // BLUR Angle (starting angle of first sample)

uniform string chromatic_aberration_info<
  string label = "Chromatic Aberration";
  string widget_type = "info";
> = "Imitates how colors diverge when light refracts. Value is between -1 and 1 as a multiplier of the displacement amount.";

uniform float chromatic_aberration<
  string label = "Chromatic Aberration (0.0)";
  string widget_type = "slider";
  float minimum = -1.0;
  float maximum = 1.0;
  float step = 0.001;
> = 0.0;

uniform string colorize_info<
  string label = "Color";
  string widget_type = "info";
> = "Imitates how light change color (tinted glass for instance)";

uniform float4 colorize_color<
  string label = "Colorize";
> = {0.0, 0.0, 0.0, 0.0};

uniform string flags_info<
  string label = "Additional Channels";
  string widget_type = "info";
> = "You can affect Blue channel (Height / Z) to displacement strength, colorization or blur radius, and Alpha to displacement (can fix glitches on edges).";

uniform bool blue_affects_strength<
    string label = "Blue channel affects displacement";
> = false;
uniform bool blue_affects_colorize<
    string label = "Blue channel affects colorize";
> = false;
uniform bool blue_affects_blur<
    string label = "Blue channel affects blur";
> = false;
uniform bool alpha_affects_strength<
    string label = "Alpha channel affects displacement";
> = false;
uniform bool apply_alpha<
    string label = "Apply alpha";
> = false;

uniform texture2d background_layer <
  string label = "Background Layer";
>;

float4 mainImage(VertData v_in) : TARGET
{
  float Pi = 6.28318530718; // Pi*2
  float blurAngleRadians = (blur_angle / 360.) * Pi;

  float4 map_rgba = image.Sample(textureSampler, v_in.uv);

  float2 displace_strength = float2(displacement_x, displacement_y) / uv_size;
  float4 displace = float4(
    (map_rgba.r * 2) - 1,
    (map_rgba.g * 2) - 1,
    (map_rgba.b * 2) - 1,
    map_rgba.a
  );

  float2 displace_uv = float2(displace.r, displace.g) * displace_strength;

  for(int p=1; p<displacement_curve; p+=1){
    displace_uv *= abs(float2(displace.r, displace.g));
  }

  if (blue_affects_strength) {
    displace_uv *= displace.b;
  }

  if (alpha_affects_strength) {
    displace_uv *= displace.a;
  }
  
  float4 base_rgba;

  if(chromatic_aberration) {
    float4 base_r = background_layer.Sample(textureSampler, v_in.uv + displace_uv - chromatic_aberration * displace_uv);
    float4 base_g = background_layer.Sample(textureSampler, v_in.uv + displace_uv);
    float4 base_b = background_layer.Sample(textureSampler, v_in.uv + displace_uv + chromatic_aberration * displace_uv);

    base_rgba = float4(base_r.r, base_g.g, base_b.b, base_g.a);
  } else {
    base_rgba = background_layer.Sample(textureSampler, v_in.uv + displace_uv);
  }

  if (blur_size > 0 && displace.a > 0) {
    float4 oc = base_rgba;
    float transparent = oc.a;
    int count = 1;
    float samples = oc.a;
    
    // Blur calculations
    [loop] for( float d=blurAngleRadians; d < Pi + blurAngleRadians; d+=Pi/blur_directions) {
      [loop] for(float i=1.0 / blur_quality; i <= 1.0; i += 1.0 / blur_quality) {
        float size = blur_size;
        float4 sc;

        if (blue_affects_blur) {
          size *= displace.b;
        }

        if (chromatic_aberration) {
          float4 sc_r = background_layer.Sample(textureSampler, v_in.uv + displace_uv - chromatic_aberration*displace_uv + float2(cos(d),sin(d)) * size * i / uv_size);
          float4 sc_g = background_layer.Sample(textureSampler, v_in.uv + displace_uv + float2(cos(d),sin(d)) * size * i / uv_size);
          float4 sc_b = background_layer.Sample(textureSampler, v_in.uv + displace_uv + chromatic_aberration*displace_uv + float2(cos(d),sin(d)) * size * i / uv_size);

          sc = float4(sc_r.r, sc_g.g, sc_b.b, sc_g.a);
        } else {
          sc = background_layer.Sample(textureSampler, v_in.uv + displace_uv + float2(cos(d),sin(d)) * size * i / uv_size);
        }

        transparent += sc.a;
        count++;
        base_rgba += sc * sc.a;
        samples += sc.a;
      }
    }

    //Calculate averages
    if (samples > 0.0)
      base_rgba /= samples;

    base_rgba.a = transparent / count; 
  }

  if (blue_affects_colorize) {
    base_rgba = lerp(base_rgba, float4(colorize_color.r, colorize_color.g, colorize_color.b, 1.0), colorize_color.a * displace.b);
  } else {
    base_rgba = lerp(base_rgba, float4(colorize_color.r, colorize_color.g, colorize_color.b, 1.0), colorize_color.a);
  }

  if (apply_alpha) {
    float4 background_rgba = background_layer.Sample(textureSampler, v_in.uv);

    return lerp(
      float4(background_rgba.r, background_rgba.g, background_rgba.b, background_rgba.a),
      float4(base_rgba.r, base_rgba.g, base_rgba.b, base_rgba.a * displace.a),
      displace.a
    );
  }
  
  return float4(base_rgba.r, base_rgba.g, base_rgba.b, base_rgba.a * displace.a);
}