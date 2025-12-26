// Quadrilateral Crop shader (inverse of a corner pin): transform a 4 points polygon to the corners of the source.
// Useful to revert perspective.

uniform float Top_Left_X<
    string label = "Top Left X";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 0;
uniform float Top_Left_Y<
    string label = "Top Left Y";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 0;
uniform float Top_Right_X<
    string label = "Top Right X";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 100.;
uniform float Top_Right_Y<
    string label = "Top Right Y";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 0;
uniform float Bottom_Left_X<
    string label = "Bottom Left X";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 0;
uniform float Bottom_Left_Y<
    string label = "Bottom Left Y";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 100.;
uniform float Bottom_Right_X<
    string label = "Bottom Right X";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 100.;
uniform float Bottom_Right_Y<
    string label = "Bottom Right Y";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100.0;
    float step = 0.01;
> = 100.;

float4 mainImage( VertData v_in ) : TARGET {
	
	float2 tl = float2(Top_Left_X, Top_Left_Y) * .01;
	float2 tr = float2(Top_Right_X, Top_Right_Y) * .01;
	float2 bl = float2(Bottom_Left_X, Bottom_Left_Y) * .01;
	float2 br = float2(Bottom_Right_X, Bottom_Right_Y) * .01;
	
	float2 t = lerp(tl, tr, v_in.uv[0]);
	float2 b = lerp(bl, br, v_in.uv[0]);
	float2 uv = lerp(t, b, v_in.uv[1]);

	return image.Sample(textureSampler, uv);
}
