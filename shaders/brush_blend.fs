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
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 applyBlend(int mode, vec4 canvas, vec3 brushRGB, float brushA) {
    vec3 brushPremul = brushRGB * brushA;
    vec3 outRGB;
    float outA;

    if (canvas.a <= 0.01) {
        outRGB = brushRGB;
        outA   = brushA;
        return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
    }

    if (mode == 0) {
        vec3 brushLin  = brushRGB * brushRGB;
        vec3 canvasLin = canvas.rgb * canvas.rgb;
        outRGB = sqrt(brushLin * brushA + canvasLin * (1.0 - brushA));
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else if (mode == 1) {
        outRGB = canvas.rgb + brushPremul;
        outA   = min(1.0, canvas.a + brushA);
    } else if (mode == 2) {
        outRGB = canvas.rgb + brushPremul * (1.0 - canvas.rgb);
        outA   = min(1.0, canvas.a + brushA);
    } else if (mode == 3) {
        outRGB = 1.0 - (1.0 - canvas.rgb) * (1.0 - brushPremul);
        outA   = 1.0 - (1.0 - canvas.a) * (1.0 - brushA);
    } else if (mode == 4) {
        outRGB = max(canvas.rgb, brushPremul);
        outA   = max(canvas.a, brushA);
    } else if (mode == 5) {
        outRGB = 1.0 - (1.0 - canvas.rgb) / (brushPremul + 0.001);
        outA   = min(1.0, canvas.a + brushA);
    } else if (mode == 6) {
        outRGB = canvas.rgb * mix(vec3(1.0), brushRGB, brushA);
        outA   = canvas.a;
    } else if (mode == 7) {
        outRGB = min(canvas.rgb, brushPremul);
        outA   = min(canvas.a, brushA);
    } else if (mode == 8) {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec2 uv = fragTexCoord;
    vec4 canvas = texture(texture0, uv);

    vec2 rMin = rectBounds.xy;
    vec2 rMax = rectBounds.xy + rectBounds.zw;

    if (uv.x < rMin.x || uv.x > rMax.x || uv.y < rMin.y || uv.y > rMax.y) {
        finalColor = canvas; return;
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
    if (dist > 1.0) { finalColor = canvas; return; }

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
    if (preserveop > 0.5) finalAlpha *= canvas.a;
    if (finalAlpha < 0.001) { finalColor = canvas; return; }

    vec3 brushFinal = brushColor.rgb;
    if (smudgeStrength > 0.001) {
        vec2 smudgeUV = clamp(uv - smudgeOffsetUV, 0.001, 0.999);
        vec4 smudgeSample = texture(texture0, smudgeUV);
        float ca = smudgeSample.a;
        brushFinal = smudgeSample.rgb * smudgeStrength + brushColor.rgb * (1.0 - smudgeStrength) * ca;
    }

    finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
}
