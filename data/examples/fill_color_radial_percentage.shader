#define PI 3.141592653589793238

uniform int Fill_Direction<
    string label = "Fill Direction";
    string widget_type = "select";
    int option_0_value = 0;
    string option_0_label = "Clockwise";
    int option_1_value = 1;
    string option_1_label = "Counter-Clockwise";
> = 0;

uniform float Fill<
    string label = "Fill";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.00005;
> = 0.0;

uniform float Start_Angle<
    string label = "Start Angle";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 720;
    float step = 1.00000;
> = 360.0;

uniform float Offset_X<
    string label = "Offset X";
    string widget_type = "slider";
    float minimum = -1.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.0;

uniform float Offset_Y<
    string label = "Offset Y";
    string widget_type = "slider";
    float minimum = -1.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.0;

uniform float4 Fill_Color;

float4 mainImage(VertData v_in) : TARGET
{
    // Calculate the center of the screen based on aspect ratio
    float aspectRatioX = uv_size.x / uv_size.y;
    float2 center = float2(0.5 * aspectRatioX + Offset_X, 0.5 + Offset_Y);

    // Normalize the UV coordinates based on aspect ratio
    float2 normalizedUV = v_in.uv * float2(aspectRatioX, 1.0);

    // Calculate the direction vector from the center to the current pixel
    float2 dir = normalizedUV - center;

    // Calculate the angle in radians
    float angle = atan2(dir.y, dir.x);

    // Convert angle from radians to degrees
    angle = degrees(angle);

    // Offset the angle to start from the specified starting angle
    angle += Start_Angle + 90.0; // Subtract 90 degrees to start at 12 o'clock
    if (angle >= 360.0)
        angle -= 360.0;

    // Adjust the angle based on the selected fill direction
    if (Fill_Direction == 1) {
        // Counter-clockwise fill
        angle = 360.0 - angle;
    }

    // Ensure angle is within [0, 360] range
    if (angle < 0.0)
        angle += 360.0;
    else if (angle >= 360.0)
        angle -= 360.0;

    // Calculate the percentage of the angle
    float anglePercentage = angle / 360.0;

    // Check if the angle percentage is within the specified "fill percentage"
    bool is_inside_fill = anglePercentage < Fill;

    // If inside the "fill," make the pixel selected color; otherwise, use the original image color
    return is_inside_fill ? Fill_Color : image.Sample(textureSampler, v_in.uv);
}
