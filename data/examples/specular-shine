// Specular Shine shader by Andicraft / Andrea Jörgensen - https://github.com/Andicraft

uniform string Hint<
    string widget_type = "info";
> = "Try using a black color source with the additive blend mode!";

uniform float roughness<
    string label = "Roughness";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.25;

uniform float lightStrength<
    string label = "lightStrength";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 2.0;
    float step = 0.001;
> = 0.5;

uniform float LightPositionX<
    string label = "Light Position X";
    string widget_type = "slider";
    float minimum = -1.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.0;

uniform float LightPositionY<
    string label = "Light Position Y";
    string widget_type = "slider";
    float minimum = -1.0;
    float maximum = 1.0;
    float step = 0.001;
> = 0.0;

uniform float flattenNormal<
    string label = "Flatten Normal";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 1.0;
    float step = 0.01;
> = 0.1;

uniform float stretchNormalX<
    string label = "Stretch Normal X";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 4.0;
    float step = 0.01;
> = 1;

uniform float stretchNormalY<
    string label = "Stretch Normal Y";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 4.0;
    float step = 0.01;
> = 1;

uniform float3 Light_Color = {1,1,1};

float Square(float a) { return a * a; }

float CookTorrance(float3 lightDir, float3 normal, float roughness) {
	float3 h = normalize(lightDir + float3(0,0,1));
	float nh2 = Square(saturate(dot(normal, h)));
	float lh2 = Square(saturate(dot(lightDir, h)));
	float r2 = Square(roughness);
	float d2 = Square(nh2 * (r2 - 1.0) + 1.00001);
	float normalization = roughness * 4.0 + 2.0;
	return r2 / (d2 * max(0.1, lh2) * normalization);
}

#define PI 3.14159265

float4 mainImage(VertData v_in) : TARGET
{

	float4 c0 = image.Sample(textureSampler, v_in.uv);

	float3 lightDir = normalize(float3(-LightPositionX*5, -LightPositionY*5, 1));

	float2 normalUV = v_in.uv - 0.5;
	normalUV.x /= stretchNormalX;
	normalUV.y /= stretchNormalY;
	normalUV += 0.5;

	float3 normal = normalize(float3(normalUV.x * 2 - 1,normalUV.y * 2 - 1,-1));
	normal.z *= -1;

	normal = lerp(normal, float3(0,0,-1), flattenNormal);

	float3 light = CookTorrance(lightDir, normal, roughness)*float3(1,1,1)*lightStrength*Light_Color;

	return float4(c0 + light,c0.a);
}
