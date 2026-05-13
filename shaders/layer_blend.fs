#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D layerTex;
uniform float layerAlpha;
uniform int bmidx;

vec4 applyBlend(int mode, vec4 dst, vec3 srcRGB, float srcA) {
    vec3 srcPremul = srcRGB * srcA;
    vec3 outRGB;
    float outA;

    // When destination is transparent, force source color directly
    if (dst.a <= 0.01) {
        outRGB = srcRGB;
        outA   = srcA;
        return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
    }

    if (mode == 0) {         // Normal gamma-correct
        vec3 srcLin = srcRGB * srcRGB;
        vec3 dstLin = dst.rgb * dst.rgb;
        outRGB = sqrt(srcLin * srcA + dstLin * (1.0 - srcA));
        outA   = srcA + dst.a * (1.0 - srcA);
    } else if (mode == 1) {  // Plus
        outRGB = dst.rgb + srcPremul;
        outA   = min(1.0, dst.a + srcA);
    } else if (mode == 2) {  // Dodge
        outRGB = dst.rgb + srcPremul * (1.0 - dst.rgb);
        outA   = min(1.0, dst.a + srcA);
    } else if (mode == 3) {  // Screen
        outRGB = 1.0 - (1.0 - dst.rgb) * (1.0 - srcPremul);
        outA   = 1.0 - (1.0 - dst.a) * (1.0 - srcA);
    } else if (mode == 4) {  // Lighten
        outRGB = max(dst.rgb, srcPremul);
        outA   = max(dst.a, srcA);
    } else if (mode == 5) {  // Burn
        outRGB = 1.0 - (1.0 - dst.rgb) / (srcPremul + 0.001);
        outA   = min(1.0, dst.a + srcA);
    } else if (mode == 6) {  // Multiply
        outRGB = dst.rgb * mix(vec3(1.0), srcRGB, srcA);
        outA   = dst.a;
    } else if (mode == 7) {  // Darken
        outRGB = min(dst.rgb, srcPremul);
        outA   = min(dst.a, srcA);
    } else if (mode == 8) {  // Normal (straight)
        outRGB = srcPremul + dst.rgb * (1.0 - srcA);
        outA   = srcA + dst.a * (1.0 - srcA);
    } else {
        outRGB = srcPremul + dst.rgb * (1.0 - srcA);
        outA   = srcA + dst.a * (1.0 - srcA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec4 dst   = texture(texture0, fragTexCoord);
    vec4 src   = texture(layerTex, fragTexCoord);
    float srcA = src.a * layerAlpha;

    if (srcA < 0.001) { finalColor = dst; return; }

    finalColor = applyBlend(bmidx, dst, src.rgb, srcA);
}
