uniform texture2d image_a;
uniform texture2d image_b;
uniform float transition_time<
    string label = "Transittion Time";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.5;
uniform bool convert_linear = true;

float4 mainImage(VertData v_in) : TARGET
{
    float4 a_val = image_a.Sample(textureSampler, v_in.uv);
	float4 b_val = image_b.Sample(textureSampler, v_in.uv);
	float4 rgba = lerp(a_val, b_val, transition_time);
    if(convert_linear)
        rgba.rgb = srgb_nonlinear_to_linear(rgba.rgb);
	return rgba;
}