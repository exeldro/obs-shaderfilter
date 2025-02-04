// RGSSAA shader by Eliseu Amaro for obs-shaderfilter plugin 2/2024
// https://github.com/exeldro/obs-shaderfilter/tree/master
// Using edge detection shader as a base, created by Hallatore
// https://forums.unrealengine.com/t/sharper-image-without-the-edge-artifacts/108461

uniform float ColorSigma<
    string label = "Color Sigma";
    string widget_type = "slider";
    float minimum = 0.1;
    float maximum = 1.0;
    float step = 0.1;
> = 1.0;

uniform float SpatialSigma<
    string label = "Spatial Sigma";
    string widget_type = "slider";
    float minimum = 0.1;
    float maximum = 1.0;
    float step = 0.1;
> = 1.0;

uniform string notes<
    string widget_type = "info";
> = "Performs RGSSAA, a form of anti-aliasing. Implementation roughly follows the original with color and spatial sigma (or strengths) parameters. Useful to apply before a sharpen pass (e.g on a webcam feed)."

float Luminance(float3 rgb)
{
    return rgb.r * 0.299 + rgb.g * 0.587 + rgb.b * 0.114;
}

float4 mainImage(VertData v_in) : TARGET
{
    float3 SceneColor = image.Sample(textureSampler, v_in.uv).rgb;
    float2 SceneUV = v_in.uv;
    float2 TexelScale = 1.0f/uv_size;

    const float SQRT2 = 1.4142135624;
    const float PI = 3.141592654;
    const float angle = PI / 8.0;
    const float cs = cos(angle);
    const float sn = sin(angle);

    // Rotated grid samples
    float3 C1 =
        image.Sample(textureSampler, SceneUV + float2(cs, -sn) * TexelScale).rgb;
    float3 C2 =
        image.Sample(textureSampler, SceneUV + float2(-cs, -sn) * TexelScale).rgb;
    float3 C3 =
        image.Sample(textureSampler, SceneUV + float2(-sn, cs) * TexelScale).rgb;
    float3 C4 =
        image.Sample(textureSampler, SceneUV + float2(sn, cs) * TexelScale).rgb;
    float3 C5 =
        image.Sample(textureSampler, SceneUV + float2(cs * SQRT2, 0) * TexelScale).rgb;
    float3 C6 =
        image.Sample(textureSampler, SceneUV + float2(0, sn *SQRT2) * TexelScale).rgb;
    float3 C7 =
        image.Sample(textureSampler, SceneUV + float2(-cs * SQRT2, 0) * TexelScale).rgb;
    float3 C8 =
        image.Sample(textureSampler, SceneUV + float2(0, -sn *SQRT2) * TexelScale).rgb;

    // Luminance edge detection
    float A0 = Luminance(SceneColor);
    float CL1 = Luminance(C1);
    float L1 = ((max(CL1, A0)) / (min(CL1, A0) + 0.001) - 1);
    float CL2 = Luminance(C2);
    float L2 = ((max(CL2, A0)) / (min(CL2, A0) + 0.001) - 1);
    float CL3 = Luminance(C3);
    float L3 = ((max(CL3, A0)) / (min(CL3, A0) + 0.001) - 1);
    float CL4 = Luminance(C4);
    float L4 = ((max(CL4, A0)) / (min(CL4, A0) + 0.001) - 1);
    float CL5 = Luminance(C5);
    float L5 = ((max(CL5, A0)) / (min(CL5, A0) + 0.001) - 1);
    float CL6 = Luminance(C6);
    float L6 = ((max(CL6, A0)) / (min(CL6, A0) + 0.001) - 1);
    float CL7 = Luminance(C7);
    float L7 = ((max(CL7, A0)) / (min(CL7, A0) + 0.001) - 1);
    float CL8 = Luminance(C8);
    float L8 = ((max(CL8, A0)) / (min(CL8, A0) + 0.001) - 1);
    float NeighborDifference = max(max(max(L1, L2), max(L3, L4)), max(max(L5, L6), max(L7, L8)));

    // Calculate distance-based weights
    float2 Dist1 = float2(cs, -sn);
    float2 Dist2 = float2(-cs, -sn);
    float2 Dist3 = float2(-sn, cs);
    float2 Dist4 = float2(sn, cs);
    float2 Dist5 = float2(cs * SQRT2, 0);
    float2 Dist6 = float2(0, sn * SQRT2);
    float2 Dist7 = float2(-cs * SQRT2, 0);
    float2 Dist8 = float2(0, -sn * SQRT2);
    float SW1 = exp(-dot(Dist1, Dist1) / (2.0 * SpatialSigma * SpatialSigma));
    float SW2 = exp(-dot(Dist2, Dist2) / (2.0 * SpatialSigma * SpatialSigma));
    float SW3 = exp(-dot(Dist3, Dist3) / (2.0 * SpatialSigma * SpatialSigma));
    float SW4 = exp(-dot(Dist4, Dist4) / (2.0 * SpatialSigma * SpatialSigma));
    float SW5 = exp(-dot(Dist5, Dist5) / (2.0 * SpatialSigma * SpatialSigma));
    float SW6 = exp(-dot(Dist6, Dist6) / (2.0 * SpatialSigma * SpatialSigma));
    float SW7 = exp(-dot(Dist7, Dist7) / (2.0 * SpatialSigma * SpatialSigma));
    float SW8 = exp(-dot(Dist8, Dist8) / (2.0 * SpatialSigma * SpatialSigma));

    // Color weights
    float3 ColorDiff1 = SceneColor.rgb - C1;
    float3 ColorDiff2 = SceneColor.rgb - C2;
    float3 ColorDiff3 = SceneColor.rgb - C3;
    float3 ColorDiff4 = SceneColor.rgb - C4;
    float3 ColorDiff5 = SceneColor.rgb - C5;
    float3 ColorDiff6 = SceneColor.rgb - C6;
    float3 ColorDiff7 = SceneColor.rgb - C7;
    float3 ColorDiff8 = SceneColor.rgb - C8;

    float CW1 = exp(-dot(ColorDiff1, ColorDiff1) / (2.0 * ColorSigma * ColorSigma));
    float CW2 = exp(-dot(ColorDiff2, ColorDiff2) / (2.0 * ColorSigma * ColorSigma));
    float CW3 = exp(-dot(ColorDiff3, ColorDiff3) / (2.0 * ColorSigma * ColorSigma));
    float CW4 = exp(-dot(ColorDiff4, ColorDiff4) / (2.0 * ColorSigma * ColorSigma));
    float CW5 = exp(-dot(ColorDiff5, ColorDiff5) / (2.0 * ColorSigma * ColorSigma));
    float CW6 = exp(-dot(ColorDiff6, ColorDiff6) / (2.0 * ColorSigma * ColorSigma));
    float CW7 = exp(-dot(ColorDiff7, ColorDiff7) / (2.0 * ColorSigma * ColorSigma));
    float CW8 = exp(-dot(ColorDiff8, ColorDiff8) / (2.0 * ColorSigma * ColorSigma));

    // Mixing weights
    float W1 = SW1 * CW1;
    float W2 = SW2 * CW2;
    float W3 = SW3 * CW3;
    float W4 = SW4 * CW4;
    float W5 = SW5 * CW5;
    float W6 = SW6 * CW6;
    float W7 = SW7 * CW7;
    float W8 = SW8 * CW8;
    float TotalWeight = W1 + W2 + W3 + W4 + W5 + W6 + W7 + W8;

    // Weighted color
    float3 AAResult = (C1 * W1 + C2 * W2 + C3 * W3 + C4 * W4 + C5 * W5 + C6 * W6 +
                    C7 * W7 + C8 * W8) /
                    max(TotalWeight, 0.0001);

    // Blend it
    float4 LuminanceNeightbors = float4(CL1, CL2, CL3, CL4);
    float4 LuminanceNeightbors2 = float4(CL5, CL6, CL7, CL8);
    float4 A0LuminanceNeightbors = abs(A0 - LuminanceNeightbors);
    float4 A0LuminanceNeightbors2 = abs(A0 - LuminanceNeightbors2);
    float A0Max = max(max(A0LuminanceNeightbors.r, A0LuminanceNeightbors.g), max(A0LuminanceNeightbors.b, A0LuminanceNeightbors.a));
    float A0Max2 = max(max(A0LuminanceNeightbors2.r, A0LuminanceNeightbors2.g), max(A0LuminanceNeightbors2.b, A0LuminanceNeightbors2.a));
    float HDREdge = max(A0Max, A0Max2);
    float EdgeMask = saturate(1.0f - HDREdge);

    return float4(lerp(SceneColor.rgb, AAResult, EdgeMask), 1.0);
}
