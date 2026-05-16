#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;   // geo UV RT
uniform sampler2D canvasTex;  // canvas copy
uniform sampler2D brushTex;   // user brush texture

uniform float opacity;
uniform float radIn;
uniform float curve;        // crv [0..1]
uniform float sol;
uniform float sol2op;
uniform float seed;
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;
uniform float texBlendVal;
uniform float texScale;
uniform vec2  texOffset;
uniform float texFeather;
uniform float texThresh;
uniform int   texBlendMode;   // 0=Mask, 1=Thr, 2=Mul
uniform int   texNoisemode;   // 0=Stencil, 1=Random, 2=Const
uniform bool  useLumAsAlpha;
uniform bool  texUseRGB;
uniform int   bmidx;
uniform vec4  brushColor;
uniform float radOut;
// canvas-space UV of this stamp's center (for noise hash)
uniform vec2  stampCenter;

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
    } else {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec2 uv      = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);

    vec4 canvas = texture(canvasTex, uv);
    vec4 geouv  = texture(texture0, sampleUV);

    if (geouv.a < 0.01) {
        finalColor = canvas;
        return;
    }

    // geo UV: [0,1] brush-local, center=0.5
    // dist in brush space: 0=center, 0.5=edge (radOut)
    vec2 p    = geouv.rg - 0.5;
    float dist = length(p);   // 0..0.5 inside brush

    // normalize to 0..1 range (0=center, 1=radOut edge)
    float d = dist * 2.0;

    // hardness
    float innerT = clamp(radIn / max(radOut, 0.001), 0.0, 1.0);
    float alpha  = 1.0;
    if (d > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.001)
            alpha = 1.0 - (d - innerT) / edgeRange;
    }
    alpha = clamp(alpha, 0.0, 1.0);

    // curvature
    float crvt      = curve * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, 3.0, crvt) : mix(1.0, 1.0/3.0, -crvt);
    alpha = clamp(pow(alpha, curvePower), 0.0, 1.0);



    float finalAlpha = clamp(alpha, 0.0, 1.0);
    if (preserveop > 0.5) finalAlpha *= canvas.a;

    // texture modulation
    vec3 brushFinal = brushColor.rgb;
    if (texBlendVal >= 0.0 && texBlendMode >= 0) {
        vec2 stUV = geouv.rg;  // stamp-local [0,1]
        if (texNoisemode == 0) {
            stUV = uv;           // canvas-absolute
        } else if (texNoisemode == 1) {
            stUV += texOffset;   // random offset
        }
        stUV = stUV * texScale;
        vec4 texel = texture(brushTex, stUV);

        if (texUseRGB) {
            brushFinal = mix(texel.rgb, texel.rgb * brushColor.rgb, texBlendVal);
        } else {
            brushFinal = brushColor.rgb;
        }

        float tex_a = useLumAsAlpha
            ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
            : texel.a;

        if (texBlendMode == 0) {
            finalAlpha *= tex_a;
        } else if (texBlendMode == 1) {
            float tresh      = 1.0 - finalAlpha;
            float treshBias  = texThresh;
            float tex_a_adj  = (treshBias < 0.0) ? (1.0 - tex_a) : tex_a;
            float bias       = (0.5 - abs(treshBias)) * 2.0;
            float power      = mix(1.0, 13.0, abs(bias));
            if (bias > 0.0) power = 1.0 / power;
            float cut      = tresh;
            float edgeDist = pow(tex_a_adj, power) - cut;
            if (texFeather <= 0.0) {
                finalAlpha = (edgeDist > 0.0) ? 1.0 : 0.0;
            } else {
                finalAlpha = clamp(edgeDist / texFeather, 0.0, 1.0);
            }
        }
        // mode 2 (Mul): no alpha change
    }

    finalAlpha *= opacity;
    if (finalAlpha < 0.000000001) { finalColor = canvas; return; }

    if (smudgeStrength > 0.000001) {
        vec2 smudgeUV    = clamp(uv - smudgeOffsetUV, 0.001, 0.999);
        vec4 smudgeSample = texture(canvasTex, smudgeUV);
        float ca = smudgeSample.a;
        brushFinal = smudgeSample.rgb * smudgeStrength
                   + brushColor.rgb * (1.0 - smudgeStrength) * ca;
    }

    finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
}
