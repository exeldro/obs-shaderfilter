uniform float speed<
    string label = "Speed";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 100;
    float step = 0.1;
> = 1;
uniform float scale<
    string label = "Scale";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 10;
    float step = 0.0001;
> = 6;
uniform float noiseLevel<
    string label = "Noise Level";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 1;
    float step = 0.001;
> = 1;
uniform bool monochromatic = false;
uniform bool use_rand = false;

float rand(float2 st)
{
  return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

float4 mainImage(VertData v_in) : TARGET
{ 
  float time = rand_activation_f + (speed / 60) * elapsed_time * 0.00001;

  if (use_rand) {
    time = rand_f;
  }

  float4 x;

  if (monochromatic) {
    x = rand(float2(v_in.uv.x, v_in.uv.y) * scale + time + scale);
  } else {
    x = float4(
      rand(float2(v_in.uv.x, v_in.uv.y) * scale + time + 1 * scale),
      rand(float2(v_in.uv.x, v_in.uv.y) * scale + time + 2 * scale),
      rand(float2(v_in.uv.x, v_in.uv.y) * scale + time + 3 * scale),
      1
    );
  }

  float4 rgba = image.Sample(textureSampler, v_in.uv);

  float3 output = lerp(rgba, x, noiseLevel);

  return float4(output.r, output.g, output.b, rgba.a);
}