// obs-shaderfilter (https://github.com/exeldro/obs-shaderfilter)
//
// Debug Float - Display a float variable onto your filter
//
// float3 displayFloat(bottomLeft, uv, value, sign, integer_length, decimal_length, scale)
//
// Parameters:
//       bottomLeft (float2) - offset from the bottom left of source to the bottom left of the text
//       uv (float2) - position on surface in pixels
//       value (float) - value to display
//       sign [0,1] - whether or not to display sign
//       integer_length (int) - length of integer field (before decimal point)
//       decimal_length (int) - length of decimal field (after decimal point)
//       scale (float) - Amount to scale the displayed text (at 1.0 the characters are sub-pixel)
//
// Conversion from shadertoy (GLSL): https://www.shadertoy.com/view/lfXfzl
//

uniform float Scale<
    string label = "Scale";
    string widget_type = "slider";
    float minimum = 1.0;
    float maximum = 50.0;
    float step = 1.0;
> = 4.0f;

uniform float OffsetX<
    string label = "OffsetX";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 50.0;
    float step = 1.0;
> = 100.0f;

uniform float OffsetY<
    string label = "OffsetY";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 50.0;
    float step = 1.0;
> = 100.0f;

// Debug Float Code Start

int getDigit(float x, int position, int sign, int integer_length, int decimal_length) {
    const int ASCII_ZERO = 0;
    const int ASCII_PERIOD = 10;
    const int ASCII_SPACE = 12;
    const int ASCII_DASH = 11;

    float absX = abs(x);
    int actualIntLength = max(1, int(log(absX) / log(10.0)) + 1);
    int totalLength = max(integer_length, actualIntLength) + ((decimal_length > 0) ? (1 + decimal_length) : 0);

    if (sign == 1) totalLength += 1;
    if (position < 0 || position >= totalLength) return ASCII_SPACE;
    if (sign == 1 && position == 0) return (x < 0.0) ? ASCII_DASH : ASCII_SPACE;
    if (sign == 1) position -= 1;

    int offset = max(0, actualIntLength - integer_length);
    int paddedPosition = position - max(0, integer_length - actualIntLength);
    if (paddedPosition < 0) return ASCII_SPACE;

    if (paddedPosition < actualIntLength) {
        int intPos = actualIntLength - 1 - paddedPosition - offset;
        int digit = int(floor(absX / pow(10.0, float(intPos)))) % 10;
        return digit + ASCII_ZERO;
    }

    if (paddedPosition == actualIntLength && decimal_length > 0) return ASCII_PERIOD;

    int fracPos = paddedPosition - actualIntLength - 1;
    if (fracPos >= 0 && fracPos < decimal_length) {
        float fracPart = absX - floor(absX);
        fracPart *= pow(10.0, float(fracPos + 1));
        int digit = int(floor(fracPart) - 10.0 * floor(floor(fracPart)/10.0));
        return digit + ASCII_ZERO;
    }

    return ASCII_SPACE;
}

float3 displayFloat(float2 bottomLeft, float2 uv, float value, int sign, int integer_length, int decimal_length, float scale) {

    // Encoded millitext characters (5x2) 
    // http://www.msarnoff.org/millitext/
    // 0123456789. -
    static const int millitext_digits[13] = {
        576339299, 536883801, 814381255, 579498183,
        617742820, 579039719, 578959843, 9114119,
        579483875, 583676131, 4096, 12583360, 0
    };

    if (scale > 1.0f) {
        uv /= scale;
        bottomLeft = floor(bottomLeft / scale);
    }

    float absX = abs(value);
    int actualIntLength = max(1, int(log(absX) / log(10.0f)) + 1);
    int totalLength = max(integer_length, actualIntLength) + ((decimal_length > 0) ? (1 + decimal_length) : 0) + ((sign == 1) ? 1 : 0);

    float3 color = float3(0.0f, 0.0f, 0.0f);
    float2 subPos = uv - bottomLeft;
    float2 upperRight = float2(2.0f * float(totalLength), 5.0f);

    if (all(subPos >= float2(0.0f, 0.0f)) && all(subPos <= upperRight)) {
        subPos.x /= 2.0f;
        subPos.y /= 5.0f;

        int p = int(floor(subPos.x)) % totalLength;
        int c = millitext_digits[getDigit(value, p, sign, integer_length, decimal_length)];

        int x = int(frac(subPos.x) * 2.0f);
        int y = int(frac(subPos.y) * 5.0f);

        int bitPos = (x * 5 + (4 - y)) * 3;
        const int mask = 7;
        int bitCol = (c >> bitPos) & mask;
        color = float3(float((bitCol >> 2) & 1), float((bitCol >> 1) & 1), float(bitCol & 1));
    }

    if (scale > 1.0f) {
        int modIndex = int(uv.x * 3.0f);
        float intensity = color[modIndex % 3];
        color = float3(intensity,intensity,intensity);
    }

    return color;
}

// Debug Float Code End

float4 mainImage(VertData v_in) : TARGET
{
    float2 fragCoord = v_in.uv * uv_size;

    // Flip Y - Required for displayFloat()
    fragCoord.y = uv_size.y - fragCoord.y;

    float2 bottomLeft = float2(OffsetX,OffsetY);

    // elapsed_time_show - elapsed time since the shader was last toggled for display
    float value = elapsed_time_show;

// displayFloat parameters
//       bottomLeft (float2) - offset from the bottom left of source to the bottom left of the text
//       uv (float2) - position on surface in pixels
//       value (float) - value to display
//       sign [0,1] - whether or not to display sign
//       integer_length (int) - length of integer field (before decimal point)
//       decimal_length (int) - length of decimal field (after decimal point)
//       scale (float) - Amount to scale the displayed text (at 1.0 the characters are sub-pixel)
    float3 textColor = displayFloat(bottomLeft, fragCoord, value,   0,   6,   4, Scale);

    float4 src = image.Sample(textureSampler, v_in.uv);

    return clamp(float4(textColor,textColor.r) + src, 0.0f, 1.0f);
}
