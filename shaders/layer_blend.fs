#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D layerTex;
uniform float layerAlpha;
uniform int bmidx;

// layer_blend.fs - straight alpha throughout, no premul assumption
void main() {
    vec4 dst = texture(texture0, fragTexCoord);
    vec4 src = texture(layerTex, fragTexCoord);

    float srcA = src.a * layerAlpha;
    vec3 srcRGB = src.rgb;

    vec3 blended;
    if (bmidx == 0) { // Normal
        blended = srcRGB;
    } else if (bmidx == 1) { // Plus / Add
        blended = dst.rgb + srcRGB * srcA;
    } else if (bmidx == 2) { // Dodge
        blended = dst.rgb + srcRGB * srcA * (1.0 - dst.rgb);
    } else if (bmidx == 3) { // Screen
        blended = 1.0 - (1.0 - dst.rgb) * (1.0 - srcRGB * srcA);
    } else if (bmidx == 4) { // Lighten
        blended = max(dst.rgb, srcRGB);
    } else if (bmidx == 5) { // Burn
        blended = 1.0 - (1.0 - dst.rgb) / (srcRGB + 0.001);
    } else if (bmidx == 6) { // Multiply
        blended = dst.rgb * srcRGB;
    } else if (bmidx == 7) { // Darken
        blended = min(dst.rgb, srcRGB);
    } else if (bmidx == 8) { // Overlay
        vec3 base = dst.rgb;
        blended = mix(2.0 * base * srcRGB, 1.0 - 2.0 * (1.0 - base) * (1.0 - srcRGB), step(0.5, base));
    } else {
        blended = srcRGB;
    }

    float outA = srcA + dst.a * (1.0 - srcA);
    vec3 outRGB = (blended * srcA + dst.rgb * dst.a * (1.0 - srcA)) / max(outA, 0.0001);

    finalColor = vec4(outRGB, outA);
}
