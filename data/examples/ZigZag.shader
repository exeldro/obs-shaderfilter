//Created by Radegast Stravinsky for obs-shaderfilter 9/2020
uniform float radius = 0.0;
uniform float angle = 180.0;
uniform float period = 0.5;
uniform float amplitude = 1.0;

uniform float center_x = 0.25;
uniform float center_y = 0.25;

uniform float phase = 1.0;
uniform int animate = 0;


uniform string notes = "Distorts the screen, creating a rippling effect that moves clockwise and anticlockwise."


float4 mainImage(VertData v_in) : TARGET
{
    float2 center = float2(center_x, center_y);
	VertData v_out;
    v_out.pos = v_in.pos;
    float2 hw = uv_size;
    float ar = 1. * hw.y/hw.x;

    v_out.uv = 1. * v_in.uv - center;
    
    center.x /= ar;
    v_out.uv.x /= ar;
    
    float dist = distance(v_out.uv, center);
    if (dist < radius)
    {
        float percent = (radius-dist)/radius;
        float theta = percent * percent * 
        (
            animate == 1 ? 
                amplitude * sin(elapsed_time) : 
                amplitude
        ) 
        * sin(percent * percent / period * radians(angle) + (phase + 
        (
            animate == 2 ? 
            elapsed_time : 
            0
        )));

        float s =  sin(theta);
        float c = cos(theta);
        v_out.uv = float2(dot(v_out.uv-center, float2(c,-s)), dot(v_out.uv-center, float2(s,c)));
        v_out.uv += (2 * center);
        
        v_out.uv.x *= ar;

        return image.Sample(textureSampler, v_out.uv);
    }
    else
    {
        return image.Sample(textureSampler, v_in.uv);
    }
        
}