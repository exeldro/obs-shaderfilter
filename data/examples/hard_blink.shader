// hard_blink shader created by https://github.com/WhazzItToYa
//
// Periodically makes the source image 100% transparent, in configurable intervals.

uniform float timeon<
    string label = "Time On";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 100.0;
    float step = 0.01;
> = 0.5;

uniform float timeoff<
    string label = "Time Off";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 100.0;
    float step = 0.01;
> = 0.5;

float4 mainImage(VertData v_in) : TARGET
{
    float4 color = image.Sample(textureSampler, v_in.uv);
    float m = timeon + timeoff;
    float t = elapsed_time % m;
    if (t < timeon) {
        return color;
    } else {
        return float4(color.r, color.g, color.b, 0.0);
    }
}
