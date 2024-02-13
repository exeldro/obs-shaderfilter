uniform float Fill<
    string label = "Fill";
    string widget_type = "slider";
    float minimum = 0;
    float maximum = 1;
    float step = 0.005;
>;
uniform int Fill_Direction<
  string label = "Fill from:";
  string widget_type = "select";
  int    option_0_value = 0;
  string option_0_label = "Left";
  int    option_1_value = 1;
  string option_1_label = "Right";  
  int    option_2_value = 2;
  string option_2_label = "Top";  
  int    option_3_value = 3;
  string option_3_label = "Bottom";
> = 0;
uniform float4 Fill_Color;

float4 mainImage(VertData v_in) : TARGET
{
    bool is_inside_fill = true;  
    
    // Check if the pixel is within the specified "fill width" on the left side
    if(Fill_Direction == 0){
        is_inside_fill = v_in.uv.x > Fill;
    }
    if(Fill_Direction == 1)
    {
        is_inside_fill = v_in.uv.x < (1.0 - Fill);
    }
    if(Fill_Direction == 2)
    {
        is_inside_fill = v_in.uv.y > Fill;
    }   
    if(Fill_Direction == 3)
    {
        is_inside_fill = v_in.uv.y < (1.0 - Fill);
    }
    
    // Invert is_inside_fill
    is_inside_fill = !is_inside_fill;
    
    // If inside the "fill," make the pixel selected colour; otherwise, use the original image color
    return is_inside_fill ? Fill_Color : image.Sample(textureSampler, v_in.uv);
}