uniform string displacement_info<
  string label = "Displacement";
  string widget_type = "info";
> = "Displaces the current image with the Mask Layer. Red channel is affected to X (horizontal) displacement and Green channel to Y (vertical). rgb(.5, .5, .5) is no displacement.";

uniform float displacement_x<
  string label = "Displacement X (px)";
> = 16.0;

uniform float displacement_y<
  string label = "Displacement Y (px)";
> = 16.0;

uniform texture2d mask_layer <string label = "Mask Layer";>;

float4 mainImage(VertData v_in) : TARGET
{
    float4 map = mask_layer.Sample(textureSampler, v_in.uv);
		float4 base = image.Sample(textureSampler, v_in.uv);

    float2 displace_strength = float2(displacement_x, displacement_y) / uv_size;
    float4 displace = float4(
      (map.r * 2) - 1,
      (map.g * 2) - 1,
      (map.b * 2) - 1,
      map.a
    );

    float2 displace_uv = float2(displace.r, displace.g) * displace_strength;
		float4 displaced = image.Sample(textureSampler, v_in.uv + displace_uv);

    return float4(displaced.r, displaced.g, displaced.b, displaced.a * displace.a);
}
