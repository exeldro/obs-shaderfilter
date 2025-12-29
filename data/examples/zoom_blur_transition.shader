//based on https://www.shadertoy.com/view/Ml3XR2

uniform texture2d image_a;
uniform texture2d image_b;
uniform float transition_time = 0.5;
uniform bool convert_linear = true;

//modified zoom blur from http://transitions.glsl.io/transition/b86b90161503a0023231
uniform float strength<
    string label = "Strength (0.3)";
    string widget_type = "slider";
    float minimum = 0.00;
    float maximum = 1.50;
    float step = 0.01;
> = 0.3;
#define PI 3.141592653589793

float Linear_ease(in float begin, in float change, in float duration, in float time) {
    return change * time / duration + begin;
}

float Exponential_easeInOut(in float begin, in float change, in float duration, in float time) {
    if (time == 0.0)
        return begin;
    else if (time == duration)
        return begin + change;
    time = time / (duration / 2.0);
    if (time < 1.0)
        return change / 2.0 * pow(2.0, 10.0 * (time - 1.0)) + begin;
    return change / 2.0 * (-pow(2.0, -10.0 * (time - 1.0)) + 2.0) + begin;
}

float Sinusoidal_easeInOut(in float begin, in float change, in float duration, in float time) {
    return -change / 2.0 * (cos(PI * time / duration) - 1.0) + begin;
}

float random(in float3 scale, in float seed) {
    return frac(sin(dot(float3(seed, seed, seed), scale)) * 43758.5453 + seed);
}

float3 crossFade(in float2 uv, in float dissolve) {
    return lerp(image_a.Sample(textureSampler, uv).rgb, image_b.Sample(textureSampler, uv).rgb, dissolve);
}

float4 mainImage(VertData v_in) : TARGET {
    float2 texCoord = v_in.uv;
	float progress = transition_time;
    // Linear interpolate center across center half of the image
    float2 center = float2(Linear_ease(0.5, 0.0, 1.0, progress),0.5);
    float dissolve = Exponential_easeInOut(0.0, 1.0, 1.0, progress);

    // Mirrored sinusoidal loop. 0->strength then strength->0
    float strength2 = Sinusoidal_easeInOut(0.0, strength, 0.5, progress);

    float3 color = float3(0.0,0.0,0.0);
    float total = 0.0;
    float2 toCenter = center - texCoord;

    /* randomize the lookup values to hide the fixed float of samples */
    float offset = random(float3(12.9898, 78.233, 151.7182), 0.0)*0.5;

    for (float t = 0.0; t <= 20.0; t++) {
        float percent = (t + offset) / 20.0;
        float weight = 1.0 * (percent - percent * percent);
        color += crossFade(texCoord + toCenter * percent * strength2, dissolve) * weight;
        total += weight;
    }
    float4 rgba = float4(color / total, 1.0);
	if (convert_linear)
		rgba.rgb = srgb_nonlinear_to_linear(rgba.rgb);
	return rgba;
}