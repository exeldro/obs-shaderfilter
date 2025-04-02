// Perspective Transform Shader for OBS
// Allows adjustable 3D perspective effects
// Usage: Add as filter in OBS via ShaderFilter plugin

uniform float angle_x<
    string label = "X Rotation";
    string widget_type = "slider";
    float minimum = -180.0;
    float maximum = 180.0;
    float step = 1.0;
> = 0.0;

uniform float angle_y<
    string label = "Y Rotation";
    string widget_type = "slider";
    float minimum = -180.0;
    float maximum = 180.0;
    float step = 1.0;
> = 0.0;

uniform float angle_z<
    string label = "Z Rotation";
    string widget_type = "slider";
    float minimum = -180.0;
    float maximum = 180.0;
    float step = 1.0;
> = 0.0;

uniform float perspective<
    string label = "Perspective Strength";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.5;

uniform float4 border_color<
    string label = "Border Color";
> = {0.0, 0.0, 0.0, 1.0};

uniform bool show_border<
    string label = "Show Border";
> = true;

float4x4 rotationMatrix(float3 angles)
{
    float radX = radians(angles.x);
    float radY = radians(angles.y);
    float radZ = radians(angles.z);
    
    float sinX = sin(radX);
    float cosX = cos(radX);
    float sinY = sin(radY);
    float cosY = cos(radY);
    float sinZ = sin(radZ);
    float cosZ = cos(radZ);
    
    return float4x4(
        cosY*cosZ, -cosY*sinZ, sinY, 0,
        sinX*sinY*cosZ + cosX*sinZ, -sinX*sinY*sinZ + cosX*cosZ, -sinX*cosY, 0,
        -cosX*sinY*cosZ + sinX*sinZ, cosX*sinY*sinZ + sinX*cosZ, cosX*cosY, 0,
        0, 0, 0, 1
    );
}

float4 mainImage(VertData v_in) : TARGET
{
    float2 uv = v_in.uv;
    
    // Center coordinates
    float2 center = float2(0.5, 0.5);
    uv -= center;
    
    // Apply perspective
    float perspectiveFactor = 1.0 / (1.0 + perspective * length(uv));
    uv *= perspectiveFactor;
    
    // Create rotation matrix
    float3 angles = float3(angle_x, angle_y, angle_z);
    float4x4 rotMat = rotationMatrix(angles);
    
    // Apply transformation
    float4 transformed = mul(rotMat, float4(uv.x, uv.y, 0, 1));
    
    // Restore center position
    uv = transformed.xy + center;
    
    // Sample texture with border handling
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return show_border ? border_color : float4(0, 0, 0, 0);
    }
    
    return image.Sample(textureSampler, uv);
}