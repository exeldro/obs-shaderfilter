// Walking Dead Pixel Fixer, Version 0.10, for OBS Shaderfilter
// by Eegee  http://github.com/eegee/
// Based on Dead Pixel Fixer, Version 0.01, for OBS Shaderfilter
// Copyright ©️ 2022 by SkeletonBow
// License: GNU General Public License, version 2
// Contact info:
//    Twitter: <https://twitter.com/skeletonbowtv>
//    Twitch: <https://twitch.tv/skeletonbowtv>
//
// Description:  Intended for use with an input source that has a dead pixel on its sensor such as a webcam.
// The pixels located in the user configured scan area and passing the threshold settings will have its colors
// overridden by taking the average of the colors of the surrounding pixels, effectively hiding the dead pixels.
//
// Changelog:
// 0.01 - Initial release
// 0.10 - Added a pixel scan area and added contrast threshold settings to replace blur size setting.

uniform int Scan_Width<
    string label = "Scan area width";
    int minimum = 1;
    int maximum = 2560;
    int step = 1;
> = 75;

uniform int Scan_Height<
    string label = "Scan area height";
    int minimum = 1;
    int maximum = 1440;
    int step = 1;
> = 120;

uniform int Scan_Offset_X<
    string label = "Scan area offset X";
    int minimum = 0;
    int maximum = 2560;
    int step = 1;
> = 110;

uniform int Scan_Offset_Y<
    string label = "Scan area offset Y";
    int minimum = 0;
    int maximum = 1440;
    int step = 1;
> = 20;

uniform bool Show_Border<
    string label = "Show scan area border in red";
    string widget_type = "checkbox";
> = true;

uniform float Contrast_Threshold<
    string label = "Contrast threshold";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.05;

uniform int Min_Cluster_Size<
    string label = "Min cluster size";
    string widget_type = "slider";
    int minimum = 0;
    int maximum = 600;
    int step = 2;
> = 324;

uniform int Max_Cluster_Size<
    string label = "Max cluster size";
    string widget_type = "slider";
    int minimum = 0;
    int maximum = 600;
    int step = 2;
> = 400;

uniform bool Show_Green<
    string label = "Show matches in green";
    string widget_type = "checkbox";
> = true;

uniform bool Bypass<
    string label = "Bypass";
    string widget_type = "checkbox";
> = false;


#define SAMPLE_RADIUS 9

float luminance(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

void sample_average(float2 uv, float2 center_uv, out float3 avgColor, out float avgLuminance, out int contrastCount, float contrastThreshold)
{
    float3 sumColor = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    contrastCount = 0;

    float3 centerColor = image.Sample(textureSampler, uv).rgb;
    float centerLum = luminance(centerColor);

    for (int y = -SAMPLE_RADIUS; y <= SAMPLE_RADIUS; ++y)
    {
        for (int x = -SAMPLE_RADIUS; x <= SAMPLE_RADIUS; ++x)
        {
            if (x == 0 && y == 0) continue;

            float2 offset = float2(x, y) / uv_size;
            float2 sample_uv = clamp(uv + offset, float2(0.0, 0.0), float2(1.0, 1.0));

            // skip central pixel
            if (ceil(sample_uv.x * uv_size.x) == ceil(center_uv.x) &&
                ceil(sample_uv.y * uv_size.y) == ceil(center_uv.y))
                continue;

            float3 sampleColor = image.Sample(textureSampler, sample_uv).rgb;
            float lum = luminance(sampleColor);

            float weight = 1.0;
            sumColor += sampleColor * weight;
            weightSum += weight;

            if (abs(lum - centerLum) >= contrastThreshold)
                contrastCount++;
        }
    }

    if (weightSum > 0)
    {
        avgColor = sumColor / weightSum;
    }
    else
    {
        avgColor = centerColor;
    }
    avgLuminance = luminance(avgColor);
}

float4 mainImage(VertData v_in) : TARGET
{
    float2 uv = v_in.uv;
    float2 pos = v_in.pos.xy;

    float4 tex = image.Sample(textureSampler, uv);
    float3 color = tex.rgb;

    if (!Bypass)
    {
        int pixX = (int)round(pos.x);
        int pixY = (int)round(pos.y);

        int borderwidth = 2;

        bool insideScan =
            (pixX >= Scan_Offset_X && pixX < Scan_Offset_X + Scan_Width) &&
            (pixY >= Scan_Offset_Y && pixY < Scan_Offset_Y + Scan_Height);

        bool borderingScan =
            (pixX >= Scan_Offset_X - borderwidth && pixX < Scan_Offset_X + Scan_Width + borderwidth) &&
            (pixY >= Scan_Offset_Y - borderwidth && pixY < Scan_Offset_Y + Scan_Height + borderwidth);

        if (insideScan)
        {
            float3 avgColor;
            float avgLum;
            int contrastCount;

            sample_average(uv, pos, avgColor, avgLum, contrastCount, Contrast_Threshold);

            if (contrastCount < Max_Cluster_Size && contrastCount >= Min_Cluster_Size)
            {
                color = avgColor;

                if (Show_Green)
                {
                    int left = pixX - borderwidth;
                    int right = pixX + borderwidth;
                    int top = pixY - borderwidth;
                    int bottom = pixY + borderwidth;

                    bool onOutline =
                        abs(pos.x - left) < borderwidth || abs(pos.x - right) < borderwidth ||
                        abs(pos.y - top) < borderwidth || abs(pos.y - bottom) < borderwidth;

                    if (onOutline)
                        return float4(0.0, 1.0, 0.0, 1.0);
                }
            }
        }
        else if (Show_Border && borderingScan)
        {
          return float4(1.0, 0.0, 0.0, 0.5);
        }
    }
    return float4(color, tex.a);
}
