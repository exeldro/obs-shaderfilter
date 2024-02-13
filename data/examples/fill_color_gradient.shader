uniform float Fill<
    string label = "Fill";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 1;
    float step = 0.005;
> = 0.500;

uniform float Gradient_Width<
    string label = "Gradient Width";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 0.15;  // Adjust the maximum value as needed
    float step = 0.01;
> = 0.05;

uniform float Gradient_Offset<
    string label = "Gradient Offset";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 0.100;  // Adjust the maximum value as needed
    float step = 0.005;
> = 0.00;

uniform int Fill_Direction<
    string label = "Fill from:";
    string widget_type = "select";
    int option_0_value = 0;
    string option_0_label = "Left";
    int option_1_value = 1;
    string option_1_label = "Right";  
    int option_2_value = 2;
    string option_2_label = "Bottom";  
    int option_3_value = 3;
    string option_3_label = "Top";
> = 0;

uniform float4 Fill_Color;

float4 mainImage(VertData v_in) : TARGET
{
    float distanceToEdge = 0.0;

    // Calculate distance to the fill edge based on the selected direction
    if (Fill_Direction == 0)
        distanceToEdge = Fill - v_in.uv.x;
    else if (Fill_Direction == 1)
        distanceToEdge = v_in.uv.x - (1.0 - Fill);
    else if (Fill_Direction == 2)
        distanceToEdge = v_in.uv.y - (1.0 - Fill);
    else if (Fill_Direction == 3)
        distanceToEdge = Fill - v_in.uv.y;

    // Calculate the gradient factor based on the distance to the edge and the gradient width
    float gradientOffset = (Fill == 0.0) ? 0.0 : (Fill == 1.0 ? 0.0 : Gradient_Offset);
    float gradientWidth = (Fill == 0.0 || Fill == 1.0) ? 0.0 : Gradient_Width;

    // Adjust distanceToEdge by the Gradient_Offset
    distanceToEdge += gradientOffset;

    // Normalize the distance to be between 0 and 1
    distanceToEdge = saturate(distanceToEdge);

    // float gradientWidth = Fill < 1.0 ? Gradient_Width : Gradient_Width * (1.0 - Fill);
    // float gradientFactor = smoothstep(0.0, gradientWidth, distanceToEdge);
    float gradientFactor = clamp(distanceToEdge / gradientWidth, 0.0, 1.0);

    // Blend between the fill color and the original image color using the gradient factor
    float4 finalColor = lerp(image.Sample(textureSampler, v_in.uv), Fill_Color, gradientFactor);

    return finalColor;
}
