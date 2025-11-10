// Audio shader example showing the difference between audio_peak and audio_magnitude.
// Left half uses audio_peak (red), right half uses audio_magnitude (blue).
uniform float audio_peak;
uniform float audio_magnitude;

uniform float intensity <
  string label = "Audio intensity";
  string widget_type = "slider";
  float minimum = 0.1;
  float maximum = 3.0;
  float step = 0.1;
> = 1.0;

float4 mainImage(VertData v_in) : TARGET {
  float4 color = image.Sample(textureSampler, v_in.uv);
  
  // Split screen based on UV coordinate
  if (v_in.uv.x < 0.5) {
    // Left half: audio_peak (instantaneous spikes, more reactive)
    // Tint with red to show peak activity.
    float peak_strength = audio_peak * intensity;
    float3 peak_color = color.rgb + float3(peak_strength, 0, 0);
    return float4(peak_color, color.a);
  } else {
    // Right half: audio_magnitude (RMS/averaged levels, smoother)
    // Tint with blue to show magnitude activity.
    float mag_strength = audio_magnitude * intensity;
    float3 mag_color = color.rgb + float3(0, 0, mag_strength);
    return float4(mag_color, color.a);
  }
}

/*
EXPLANATION:
- audio_peak: Shows instantaneous maximum levels, very responsive to drums/percussion.
- audio_magnitude: Shows RMS (Root Mean Square) levels, smoother and represents sustained audio.

TYPICAL BEHAVIOR:
- With music containing drums: Left side (peak) will flash more dramatically on beats.
- With sustained tones: Right side (magnitude) will show more consistent levels.
- Peak reacts faster to sudden sounds, magnitude is more stable for smooth effects.
*/
