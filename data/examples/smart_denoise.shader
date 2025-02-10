// Smart DeNoise By Michele Morrone (https://github.com/BrutPitt/glslSmartDeNoise)  July 15, 2024
// Converted to OBS version of HLSL by Euiko on February 10, 2025

#define INV_SQRT_OF_2PI 0.39894228040143267793994605993439  // 1.0/SQRT_OF_2PI
#define INV_PI          0.31830988618379067153776752674503

uniform float uSigma<
    string label = "Sigma";
    string widget_type = "slider";
    float minimum = 0.01;
    float maximum = 3;  // max based on the webgl sample, which is 3 
    float step = 0.01;
> = 1.0;  // default value set to 1 to make it lighter
uniform float uKSigma<
    string label = "K-Sigma";
    string widget_type = "slider";
    float minimum = 0.01;
    float maximum = 24; // max based on the webgl sample, which is 24 
    float step = 0.01;
> = 7.0;  // the default value is based on the webgl sample
uniform float uThreshold<
    string label = "Edge Threshold";
    string widget_type = "slider";
    float minimum = 0.01;
    float maximum = 2;  // max based on the webgl sample, which is 2
    float step = 0.01;
> = 0.190;  // the default value is based on the webgl sample

//  smartDeNoise - parameters
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  float2 uv         - actual fragment coord
//  float2 size       - window size
//  float sigma  >  0 - sigma Standard Deviation
//  float kSigma >= 0 - sigma coefficient 
//      kSigma * sigma  -->  radius of the circular kernel
//  float threshold   - edge sharpening threshold 
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// NOTE: image's texture2d data will be supplied by the OBS shaderfilter by default
float4 smartDeNoise(
    float2 uv,
    float2 size,
    float sigma,
    float kSigma,
    float threshold
) {
    float radius = round(kSigma * sigma);
    float radQ = radius * radius;

    float invSigmaQx2 = 0.5 / (sigma * sigma);   // 1.0 / (sigma^2 * 2.0)
    float invSigmaQx2PI = INV_PI * invSigmaQx2;  // 1/(2 * PI * sigma^2)

    float invThresholdSqx2 = 0.5 / (threshold * threshold);   // 1.0 / (sigma^2 * 2.0)
    float invThresholdSqrt2PI = INV_SQRT_OF_2PI / threshold;  // 1.0 / (sqrt(2*PI) * sigma^2)

    float4 centrPx = image.Sample(textureSampler, uv);

    float zBuff = 0.0;
    float4 aBuff = float4(0.0, 0.0, 0.0, 0.0);

    float2 d;
    for (d.x = -radius; d.x <= radius; d.x += 1.0)
    {
        float pt = sqrt(radQ - (d.x * d.x));  // pt = yRadius: have circular trend
        d.y = -pt;
        for (; d.y <= pt; d.y += 1.0)
        {
            float blurFactor = exp((-dot(d, d)) * invSigmaQx2) * invSigmaQx2PI;
            float4 walkPx = image.Sample(textureSampler, uv + (d / size));
            float4 dC = walkPx - centrPx;
            float deltaFactor = (exp((-dot(dC.xyz, dC.xyz)) * invThresholdSqx2) * invThresholdSqrt2PI) * blurFactor;
            zBuff += deltaFactor;
            aBuff += (walkPx * deltaFactor);
        }
    }
    return aBuff / float4(zBuff, zBuff, zBuff, zBuff);
}

float4 mainImage(VertData v_in) : TARGET
{
    return smartDeNoise(v_in.uv, uv_size, uSigma, uKSigma, uThreshold);
}
