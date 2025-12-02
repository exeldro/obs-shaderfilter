// ASCII shader for use with obs-shaderfilter 7/2020 v1.0
// https://github.com/Oncorporation/obs-shaderfilter
// Based on the following shaders:
// https://www.shadertoy.com/view/3dtXD8 - Created by DSWebber in 2019-10-24
// https://www.shadertoy.com/view/lssGDj - Created by movAX13h in 2013-09-22

// Modifications of original shaders include:
//  - Porting from GLSL to HLSL
//  - Combining characters sets from both source shaders
//  - Adding support for parameters from OBS for monochrome rendering, scaling and dynamic character set
//
// Add Additional Characters with this tool: http://thrill-project.com/archiv/coding/bitmap/
// converts a bitmap into int then decodes it to look like text

uniform int scale<
    string label = "Scale";
    string widget_type = "slider";
    int minimum = 1;
    int maximum = 20;
    int step = 1;
> = 1; // Size of characters
uniform float4 base_color<
    string label = "Base color";
> = {0.0,1.0,0.0,1.0}; // Monochrome base color
uniform bool monochrome<
    string label = "Monochrome";
> = false;
uniform int character_set<
  string label = "Character set";
  string widget_type = "select";
  int    option_0_value = 0;
  string option_0_label = "Large set of non-letters";
  int    option_1_value = 1;
  string option_1_label = "Small set of capital letters";
> = 0;
uniform string note<
    string widget_type = "info";
> = "Base color is used as monochrome base color.";

float character(int n, float2 p)
{
    p = floor(p*float2(4.0, 4.0) + 2.5);
    if (clamp(p.x, 0.0, 4.0) == p.x)
    {
        if (clamp(p.y, 0.0, 4.0) == p.y)	
        {
	    int a = int(round(p.x) + 5.0 * round(p.y));
            if (((n >> a) & 1) == 1) return 1.0;
        }	
    }
    return 0.0;
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x/y);
}

float4 mainImage( VertData v_in ) : TARGET
{
    float2 iResolution = uv_size;
    float2 pix = v_in.uv * iResolution;
    float4 c = image.Sample(textureSampler, floor(pix/float2(scale*8.0,scale*8.0))*float2(scale*8.0,scale*8.0)/iResolution.xy);

    float gray = 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
	
    int n;
    int charset = clamp(character_set, 0, 1);

    if (charset==0)
    {
        if (gray <= 0.2) n = 4096;     // .
        if (gray > 0.2)  n = 65600;    // :
        if (gray > 0.3)  n = 332772;   // *
        if (gray > 0.4)  n = 15255086; // o 
        if (gray > 0.5)  n = 23385164; // &
        if (gray > 0.6)  n = 15252014; // 8
        if (gray > 0.7)  n = 13199452; // @
        if (gray > 0.8)  n = 11512810; // #
    }
    else if (charset==1)
    {
        if (gray <= 0.1) n = 0;
        if (gray > 0.1)  n = 9616687; // R
        if (gray > 0.3)  n = 32012382; // S
        if (gray > 0.5)  n = 16303663; // D
        if (gray > 0.7)  n = 15255086; // O
        if (gray > 0.8)  n = 16301615; // B
    }

    float2 p = mod(pix/float2(scale*4.0,scale*4.0),float2(2.0,2.0)) - float2(1.0,1.0);
	
    if (monochrome)
    {
        c.rgb = base_color.rgb;
    }
    c = c*character(n, p);
    
    return c;
}
