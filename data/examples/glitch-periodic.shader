// Created by Éric Nicolas (ccjmne) for use with obs-shaderfilter 12/2025
// Port of:                https://www.shadertoy.com/view/WfVfDh
// Originally forked from: https://www.shadertoy.com/view/MtXBDs

#define PI 3.14159265359

/*           For visual explanation of the paramters, see            */
/*           https://www.desmos.com/calculator/vezu1wyqma            */
/*                                                                   */
/*  Period        How often a glitch occurs  (in seconds)  0–?       */
/*  Duration      How long a glitch lasts    (in seconds)  0–Period  */
/*  Amplitude     How intense a glitch is                  0–1       */
/*  Scratchiness  How jittery a glitch is                  0–1       */

uniform float PERI<
    string label       = "Period";
    string widget_type = "slider";
    float minimum      = 1.;
    float maximum      = 60.;
    float step         = 1.;
> = 6.;

uniform float DURA<
    string label       = "Duration";
    string widget_type = "slider";
    float minimum      = 0.;
    float maximum      = 60.;
    float step         = .01;
> = .5;

uniform float AMPL<
    string label       = "Amplitude";
    string widget_type = "slider";
    float minimum      = 0.;
    float maximum      = 1.;
    float step         = .01;
> = .15;

uniform float SCRA<
    string label       = "Scratchiness";
    string widget_type = "slider";
    float minimum      = 0.;
    float maximum      = 1.;
    float step         = .01;
> = .2;

float random2d(float2 n) {
    return frac(sin(dot(n, float2(12.9898, 4.1414))) * 43758.5453);
}

float randomRange(in float2 seed, in float lo, in float hi) {
    return lo + random2d(seed) * (hi - lo);
}

float insideRange(float v, float bottom, float top) {
    return step(bottom, v) - step(top, v);
}

float4 mainImage(VertData v_in): TARGET {
    float time = floor(elapsed_time * SCRA * 60.);
    float2 uv = v_in.uv;

    // Periodic intermittence
    float AMP = AMPL * (cos(2. * PI * max(0., (mod(-elapsed_time / PERI, 1.) - 1.) * PERI / DURA + 1.)) * -.5 + .5);

    float4 outCol = image.Sample(textureSampler, uv);

    // Randomly offset slices horizontally
    float offsetMax = AMP / 2.;
    for (float i = 0.; i < 10. * AMP; i += 1.) {
        float sliceY =  random2d(   float2(time, 2345. + i));
        float sliceH =  random2d(   float2(time, 9035. + i)) * .25;
        float offsetH = randomRange(float2(time, 9625. + i), -offsetMax, offsetMax);
        float2 uvOff = uv;
        uvOff.x += offsetH;
        if (insideRange(uv.y, sliceY, frac(sliceY + sliceH)) == 1.) {
            outCol = image.Sample(textureSampler, uvOff);
        }
    }

    // Slightly offset one entire channel
    offsetMax = AMP / 6.;
    float2 colOff = float2(
        randomRange(float2(time, 9545.), -offsetMax, offsetMax),
        randomRange(float2(time, 7205.), -offsetMax, offsetMax)
    );
    float rnd = random2d(float2(time , 9545.));
    if      (rnd < .33) outCol.r = image.Sample(textureSampler, uv + colOff).r;
    else if (rnd < .66) outCol.g = image.Sample(textureSampler, uv + colOff).g;
    else                outCol.b = image.Sample(textureSampler, uv + colOff).b;

    return outCol;
}
