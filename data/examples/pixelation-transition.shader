uniform float transition_time<
    string label = "Transittion Time";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.5;
uniform bool convert_linear = true;
uniform float power<
    string label = "Power";
    string widget_type = "slider";
    float minimum = 0.5;
    float maximum = 8.0;
    float step = 0.01;
> = 3.0;
uniform float center_x<
    string label = "X";
    string widget_type = "slider";
	string group = "Center";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.5;
uniform float center_y<
    string label = "Y";
    string widget_type = "slider";
	string group = "Center";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.5;

float4 mainImage(VertData v_in) : TARGET
{
	//1..0..1
	float scale = abs(transition_time - 0.5) * 2.0;
	scale = pow(scale, power);

	float2 uv = v_in.uv;
	uv -= float2(center_x, center_y);
	uv *= uv_size;
	uv *= scale;
	uv = floor(uv);
	uv /= scale;
	uv /= uv_size;
	uv += float2(center_x, center_y);
	uv = clamp(uv, 1.0/uv_size, 1.0);
	float4 rgba = image.Sample(textureSampler, uv);
	if(convert_linear)
        rgba.rgb = srgb_nonlinear_to_linear(rgba.rgb);
	return rgba;
}
