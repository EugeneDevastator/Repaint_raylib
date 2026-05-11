#version 330

// ============================================================================
// FULL BRUSH MASK SHADER
// - Hardness (rad_in / rad_out ratio)
// - Curvature (bubble/pinch)
// - Aspect ratio (x2y)
// - Rotation (resangle)
// - Solidity noise scatter
// - Multiple blend modes
// ============================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;

uniform vec4 rectBounds;
uniform float radIn;
uniform float radOut;
uniform float opacity;
uniform vec4 brushColor;
uniform float crv;
uniform float x2y;
uniform float resangle;
uniform float sol;
uniform float sol2op;
uniform int bmidx;
uniform float seed;

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 applyBlend(int mode, vec4 dst, vec3 srcRGB, float srcA) {
    vec3 srcPremul = srcRGB * srcA;
    vec3 outRGB;
    float outA;

    if (mode == 0) {         // Normal gamma-correct
        vec3 srcLin = srcRGB * srcRGB;
        vec3 dstLin = dst.rgb * dst.rgb;
		
		if(dst.a <= 0.11)
		outRGB = srcRGB;
		else
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
    vec2 uv = fragTexCoord;
    vec4 dst = texture(texture0, uv);

    vec2 rMin = rectBounds.xy;
    vec2 rMax = rectBounds.xy + rectBounds.zw;

    if (uv.x < rMin.x || uv.x > rMax.x || uv.y < rMin.y || uv.y > rMax.y) {
        finalColor = dst; return;
    }

    vec2 center   = (rMin + rMax) * 0.5;
    vec2 halfSize = (rMax - rMin) * 0.5;
    vec2 diff;
    diff.x = (uv.x - center.x) / max(halfSize.x, 0.001);
    diff.y = (uv.y - center.y) / max(halfSize.y, 0.001);
    diff.x /= max(x2y, 0.01);

    float angle = radians(resangle);
    vec2 rdiff;
    rdiff.x = diff.x * cos(angle) - diff.y * sin(angle);
    rdiff.y = diff.x * sin(angle) + diff.y * cos(angle);

    float dist = length(rdiff);
    if (dist > 1.0) { finalColor = dst; return; }

    float innerT = clamp(radIn / max(radOut, 0.001), 0.0, 1.0);
    float alpha = 1.0;
    if (dist > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.001)
            alpha = 1.0 - (dist - innerT) / edgeRange;
    }
    alpha = clamp(alpha, 0.0, 1.0);

    float crvt = crv * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, 3.0, crvt) : mix(1.0, 1.0/3.0, -crvt);
    alpha = clamp(pow(alpha, curvePower), 0.0, 1.0);

    if (!(sol >= 0.999 && sol2op <= 0.001)) {
        float noiseVal = hash2(uv * 1000.0 + floor(seed * 1000.0));
        float fsol2op  = clamp(1.0 - sol2op, 0.0, 1.0);
        float nsal     = (noiseVal < sol)   ? 1.0 : 0.0;
        float noiseres = (noiseVal < alpha) ? 1.0 : 0.0;
        nsal  = noiseres * fsol2op + nsal * (1.0 - fsol2op);
        alpha *= nsal;
    }

    float finalAlpha = clamp(alpha, 0.0, 1.0) * opacity;
    if (finalAlpha < 0.001) { finalColor = dst; return; }

    finalColor = applyBlend(bmidx, dst, brushColor.rgb, finalAlpha);
}
