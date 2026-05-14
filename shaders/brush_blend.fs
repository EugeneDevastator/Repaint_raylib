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
uniform sampler2D brushTex;
uniform float   texBlendVal;
uniform float   texScale;
uniform vec2    texOffset;
uniform float   texFeather;
uniform float   texThresh;
uniform int     texBlendMode;   // 0=Mask, 1=Thr, 2=Mul
uniform int     texNoisemode;   // 0=Stencil, 1=Random, 2=Const
uniform bool    useLumAsAlpha;
uniform bool    texUseRGB;

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 applyBlend(int mode, vec4 canvas, vec3 brushRGB, float brushA) {
    vec3 brushPremul = brushRGB * brushA;
    vec3 outRGB;
    float outA;

    if (canvas.a <= 0.00000001) {
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

    float finalAlpha = clamp(alpha, 0.0, 1.0);
    if (preserveop > 0.5) finalAlpha *= canvas.a;

    // ── Texture modulation (0=Mask, 1=Thr, 2=Mul) ─────────────────
    vec3 brushFinal = brushColor.rgb;
    if (texBlendVal >= 0.0 && texBlendMode >= 0) {
        vec2 stUV = (uv - rectBounds.xy) / rectBounds.zw;
        // Noisemode: 0=Stencil (canvas-absolute UV), 1=Random, 2=Const (stamp-local)
        if (texNoisemode == 0) {
            stUV = uv;
        } else if (texNoisemode == 1) {
            stUV += texOffset;
        }
        stUV = stUV * texScale;
        vec4 texel = texture(brushTex, stUV);

        // RGB: use texture color or brush color only
        if (texUseRGB) {
            brushFinal = mix(texel.rgb, texel.rgb * brushColor.rgb, texBlendVal);
        } else {
            brushFinal = brushColor.rgb;
        }

        // Texture alpha source: average luminance or native alpha
        float tex_a = useLumAsAlpha
            ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
            : texel.a;

        // Blend mode determines how texture alpha gates the brush mask
        if (texBlendMode == 0) {
            // Mask: smooth alpha gate
            finalAlpha *= tex_a;
} else if (texBlendMode == 1) {
    float cutTexA = (texThresh >= 0.0) ? tex_a : (1.0 - tex_a);
    float t = abs(texThresh);
    float blend = 1.0 - 2.0 * abs(t - 0.5);
    float cut = mix(t, 1.0 - finalAlpha, blend);  // inverted finalAlpha
    float edgeDist = cutTexA - cut;
    if (texFeather <= 0.0) {
        finalAlpha = (edgeDist > 0.0) ? 1.0 : 0.0;
    } else {
        finalAlpha = clamp(edgeDist / texFeather, 0.0, 1.0);
    }
}
else {
            // Multiply: no alpha gating from texture (full brush alpha)
        }
    }

    // Apply opacity slider at the end so it never affects threshold or mask calculations
    finalAlpha *= opacity;

    if (finalAlpha < 0.000000001) { finalColor = canvas; return; }

    if (smudgeStrength > 0.000001) {
        vec2 smudgeUV = clamp(uv - smudgeOffsetUV, 0.001, 0.999);
        vec4 smudgeSample = texture(texture0, smudgeUV);
        float ca = smudgeSample.a;
        brushFinal = smudgeSample.rgb * smudgeStrength + brushColor.rgb * (1.0 - smudgeStrength) * ca;
    }

    finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
}
