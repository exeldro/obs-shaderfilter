/*
 * ------------------------------------------------------------
 * "THE BEERWARE LICENSE" (Revision 42):
 * maple <maple@maple.pet> wrote this code. As long as you retain this 
 * notice, you can do whatever you want with this stuff. If we
 * meet someday, and you think this stuff is worth it, you can
 * buy me a beer in return.
 * ------------------------------------------------------------
 * from https://www.shadertoy.com/view/3tSXRh 
 * adopted for OBS by Exeldro
 * ------------------------------------------------------------
 */

uniform float pixelSize<
    string label = "Pixel Size";
    string widget_type = "slider";
    float minimum = 1.0;
    float maximum = 50.0;
    float step = 0.1;
> = 3.0; 

uniform float dither_factor<
    string label = "Dither Factor";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 10.0;
    float step = 0.01;
> = 0.8;

uniform bool alternative_bayer;

uniform float brightness<
    string label = "Brightness";
    string widget_type = "slider";
    float minimum = -1.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.0;
uniform float contrast<
    string label = "Contrast";
    string widget_type = "slider";
    float minimum = -10.0;
    float maximum = 10.0;
    float step = 0.01;
> = 1.0;
uniform float gamma<
    string label = "Gamma";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 10.0;
    float step = 0.01;
> = 0.6;

uniform float4 color_1 = {0.18, 0, 0.18, 1.0};
uniform float4 color_2 = {0.37, 0.15, 0.47, 1.0};
uniform float4 color_3 = {0.97, 0.56, 0.12, 1.0};
uniform float4 color_4 = {0.97, 0.94, 0.53, 1.0};

// quantize coords to low resolution
float2 pixelize(float2 uv, float2 pixelSize) {
    float2 factor = pixelSize / uv_size;
    return floor(uv / factor) * factor;
}

float3 colorLUT(float3 color) {
    float gray = color.r*0.3 + color.g*0.59 + color.b*0.11;
    if(gray < 0.25)
        return color_1.rgb;
    if(gray < 0.50)
        return color_2.rgb;
    if(gray < 0.75)
        return color_3.rgb;
    return color_4.rgb;
}

// adjust brightness, contrast and gamma levels of a color
float3 levels(float3 color, float brightness, float contrast, float3 gamma) {
    float3 value = (color - 0.5) * contrast + 0.5;
    value = clamp(value + brightness, 0.0, 1.0);
    return clamp(float3(pow(abs(value.r), gamma.x),pow(abs(value.g), gamma.y),pow(abs(value.b), gamma.z)), 0.0, 1.0);
}
float3 levels(float3 color, float brightness, float contrast, float gamma) { 
    return levels(color, brightness, contrast, float3(gamma, gamma, gamma));
}

// applies the dithering filter to a color map
float3 dither8x8(float2 coord, float3 color, float2 pixelSize) {
    // reduces pixel space to the selected pixel size
    float2 pixelCoord = floor((coord * uv_size) / pixelSize + float2(0.5, 0.5));
   
    // applies the bayer matrix filter to the color map
    pixelCoord = pixelCoord - 8.0 * floor(pixelCoord/8.0);
    int index = int(pixelCoord.x + (pixelCoord.y * 8.0));
    float bayer;
    if (alternative_bayer){
#ifdef OPENGL
        const int[64] bayer8 = int[64](
#else
        const int bayer8[64] = {
#endif
            0, 32,  8, 40,  2, 34, 10, 42, /* 8x8 Bayer ordered dithering */
            48, 16, 56, 24, 50, 18, 58, 26, /* pattern. Each input pixel */
            12, 44,  4, 36, 14, 46,  6, 38, /* is scaled to the 0..63 range */
            60, 28, 52, 20, 62, 30, 54, 22, /* before looking in this table */
            3, 35, 11, 43,  1, 33,  9, 41, /* to determine the action. */
            51, 19, 59, 27, 49, 17, 57, 25,
            15, 47,  7, 39, 13, 45,  5, 37,
            63, 31, 55, 23, 61, 29, 53, 21
#ifdef OPENGL
        );
#else
        };
#endif
        bayer = (bayer8[index]-31.0)/32.0;
    } else {
#ifdef OPENGL
        const int[64] bayer8 = int[64](
#else
        const int bayer8[64] = {
#endif
            0, 48, 12, 60, 3, 51, 15, 63,
            32, 16, 44, 28, 35, 19, 47, 31,
            8, 56, 4, 52, 11, 59, 7, 55,
            40, 24, 36, 20, 43, 27, 39, 23,
            2, 50, 14, 62, 1, 49, 13, 61,
            34, 18, 46, 30, 33, 17, 45, 29,
            10, 58, 6, 54, 9, 57, 5, 53,
            42, 26, 38, 22, 41, 25, 37, 21
#ifdef OPENGL
        );
#else
        };
#endif
        bayer = (bayer8[index]-31.0)/32.0;
    }
    float3 bayerColor = (color + float3(bayer,bayer,bayer) * (dither_factor / 8.0));
    // limits it to the selected palette
    color = colorLUT(bayerColor);

    return color;
}

float4 mainImage(VertData v_in) : TARGET
{
    float2 texcoord = pixelize(v_in.uv, float2(pixelSize,pixelSize));
    texcoord = clamp(texcoord, 0.001, 1.0);
    float4 c = image.Sample(textureSampler, texcoord);
    float3 color = c.rgb;
   
    color = levels(color, brightness, contrast, float3(gamma, gamma, gamma));
    
    color = dither8x8(texcoord, color, float2(pixelSize,pixelSize));
    
    return float4(color.r, color.g, color.b, c.a);
}
