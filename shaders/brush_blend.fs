#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec2 localUV;
out vec4 finalColor;

uniform sampler2D texture0;       // canvas (brushTempRT)
uniform sampler2D brushGeoUvTex;  // unit 1: stampRT — rg=UV, always alpha=1 inside quad
uniform sampler2D brushTex;       // unit 2: user brush mask texture

uniform float radIn;
uniform float radOut;
uniform float opacity;
uniform vec4  brushColor;
uniform float crv;
uniform float sol;
uniform float sol2op;
uniform int   bmidx;
uniform float seed;
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;

uniform float texBlendVal;
uniform float texScale;
uniform vec2  texOffset;
uniform float texFeather;
uniform float texThresh;
uniform int   texBlendMode;
uniform int   texNoisemode;
uniform bool  useLumAsAlpha;
uniform bool  texUseRGB;

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 applyBlend(int mode, vec4 canvas, vec3 brushRGB, float brushA) {
    vec3 brushPremul = brushRGB * brushA;
    vec3 outRGB;
    float outA;

    if (canvas.a <= 0.00000001) {
        return vec4(clamp(brushRGB, 0.0, 1.0), clamp(brushA, 0.0, 1.0));
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

    // Circle SDF in stamp space — localUV is [-1,1] relative to stamp center
    // x2y squeeze is baked into stampRT, so localUV uses symmetric radii
    float dist = length(localUV);
    if (dist > 1.0) { finalColor = canvas; return; }

    // Hardness falloff
    float inner = radIn / max(radOut, 0.001);
    float alpha;
    if (dist <= inner) {
        alpha = 1.0;
    } else {
        alpha = 1.0 - (dist - inner) / max(1.0 - inner, 0.000001);
    }
    alpha = clamp(alpha, 0.0, 1.0);

    // Curvature
    float crvt = crv * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, 3.0, crvt) : mix(1.0, 1.0/3.0, -crvt);
    alpha = clamp(pow(alpha, curvePower), 0.0, 1.0);

    // Solidity noise
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

    vec3 brushFinal = brushColor.rgb;

    // Sample stampRT to get UV coords for brushTex
    vec2 stUV = localUV * 0.5 + 0.5;
    vec4 geoSample = texture(brushGeoUvTex, stUV);
    vec2 brushTexUV = geoSample.rg; // UV baked by xform pass

    // User brush mask texture
    // User brush mask texture
    if (texBlendVal >= 0.0 && texBlendMode >= 0) {
        vec2 maskUV = brushTexUV - 0.5;          // center
        if (texNoisemode == 1) maskUV += texOffset;
        maskUV = maskUV / max(texScale, 0.001) + 0.5;  // scale from center
        vec4 texel = texture(brushTex, maskUV);

        if (texUseRGB) {
            brushFinal = mix(texel.rgb, texel.rgb * brushColor.rgb, texBlendVal);
        }

        float tex_a = useLumAsAlpha
            ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
            : texel.a;

        if (texBlendMode == 0) {
            finalAlpha *= tex_a;
        } else if (texBlendMode == 1) {
            float tresh    = 1.0 - finalAlpha;
            float treshBias = texThresh;
            float tex_a_adj = (treshBias < 0.0) ? (1.0 - tex_a) : tex_a;
            float bias = (0.5 - abs(treshBias)) * 2.0;
            float power = mix(1.0, 13.0, abs(bias));
            if (bias > 0.0) power = 1.0 / power;
            float edgeDist = pow(tex_a_adj, power) - tresh;
            if (texFeather <= 0.0) {
                finalAlpha = (edgeDist > 0.0) ? 1.0 : 0.0;
            } else {
                finalAlpha = clamp(edgeDist / texFeather, 0.0, 1.0);
            }
        }
    }

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
