#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D canvasTex;
uniform sampler2D brushTex;

uniform float opacity;
uniform float radIn;
uniform float curve;
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
uniform int   texBlendMode;
uniform int   texNoisemode;
uniform bool  useLumAsAlpha;
uniform int   texColorMode;
uniform int   bmidx;
uniform vec4  brushColor;
uniform float radOut;
uniform vec2  stampCenter;
uniform vec2  canvasSize;
uniform float pwr;
uniform int   eraseMode;

in vec2 canvasFragUV;

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
// Returns hard or soft threshold cut based on texFeather
// combined: input mask value [0..1]
// cut: abs(texThresh), threshold level
// texFeather: softness of edge (0 = hard cut)
float applyThreshold(float combined, float cut, float texFeather) {
    float edgeDist = combined - cut;
    if (texFeather <= 0.0)
        return (edgeDist > 0.0) ? 1.0 : 0.0;
    return clamp(edgeDist / max(texFeather, 0.0001), 0.0, 1.0);
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
    } else if (mode == 9) {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else if (mode == 10) {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else if (mode == 11) {
        outRGB = mix(canvas.rgb, vec3(1.0) - canvas.rgb, brushA);
        outA   = canvas.a;
    } else if (mode == 12) {
        outRGB = abs(canvas.rgb - brushRGB);
        outA   = canvas.a;
    } else if (mode == 13) {
        outRGB = canvas.rgb + brushRGB - 2.0 * canvas.rgb * brushRGB;
        outA   = canvas.a;
    } else if (mode == 14) {
        outRGB = canvas.rgb;
        outA   = canvas.a * (1.0 - brushA);
    } else if (mode == 15) {
        float gamma = 2.2;
        float eraseMask = pow(canvas.a, gamma) * brushA;
        outRGB = mix(canvas.rgb, brushRGB, eraseMask);
        outA   = canvas.a * (1.0 - brushA * 0.5);
    } else {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

float applyRadialFalloff(float d) {
    if (d<0.0000000001) return 0.0;
    float innerT = clamp(radIn, 0.0, 1.0); // already normalized [0..1]
    float a = 1.0;
    if (d > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.001)
            a = 1.0 - (d - innerT) / edgeRange;
    }
    a = clamp(a, 0.0, 1.0);
    float crvt      = curve * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, 3.0, crvt) : mix(1.0, 1.0/3.0, -crvt);
    return clamp(pow(a, curvePower), 0.0, 1.0);
}


void main() {
    vec2 uv       = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);
    vec4 geouv    = texture(texture0, sampleUV);

    vec2 canvasUV = canvasFragUV;
    canvasUV.y *= -1;
    vec4 canvas = texture(canvasTex, canvasUV);

    if (geouv.a < 0.01) {
        finalColor = texture(canvasTex, canvasUV);
        return;
    }

    vec2  p    = geouv.rg - 0.5;
    float dist = length(p);
    float d    = dist * 2.0;

    float alpha = applyRadialFalloff(d);

    // --- texture sampling ---
    vec4 texel = vec4(1.0);
    if (texBlendMode >= 0) {
        // Default: stamp-local UV (brush space 0..1), same every stamp = CONST
        vec2 stUV = geouv.rg;

        if (texNoisemode == 0) {
            // STENCIL (0): canvas-absolute UV, texture locked to canvas world space
            stUV = canvasFragUV;
        } else if (texNoisemode == 1) {
            // RANDOM (1): stamp-local + per-stamp random offset via texOffset
            stUV = geouv.rg + texOffset;
        }
        // CONST (2): stUV = geouv.rg, no offset, identical every stamp

        stUV = stUV * texScale;
        texel = texture(brushTex, stUV);
    }

    // --- userTexA ---
    float userTexA = useLumAsAlpha
        ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
        : texel.a;

    if (texThresh < 0.0)
        userTexA = 1.0 - userTexA;

    // --- masks ---
    float firstMask;
    float secondMask;

    if (texBlendMode == 0) {
        firstMask  = userTexA * alpha;
        secondMask = 1.0;
    } else if (texBlendMode == 2) {
        firstMask  = applyThreshold(userTexA, abs(texThresh), texFeather);
        firstMask  = applyRadialFalloff(firstMask);
        secondMask = 1;
    } else {
        // Threshold
        firstMask  = alpha;
        secondMask = userTexA;
    }

    // --- combine first and second ---
    float finalAlpha;
    if (texBlendMode == 2) {
        finalAlpha = firstMask;
    }
    else if (texBlendMode == 1) {
        float combined = firstMask * secondMask;
        finalAlpha = applyThreshold(combined, abs(texThresh), texFeather);
    } else {
        finalAlpha = firstMask * secondMask;
    }

    finalAlpha = clamp(finalAlpha, 0.0, 1.0);


    // --- brush color ---
    vec3 brushFinal;
    if (texColorMode == 0) {
        brushFinal = brushColor.rgb;
    } else if (texColorMode == 1) {
        brushFinal = texel.rgb;
    } else {
        brushFinal = texel.rgb * brushColor.rgb;
    }

    finalAlpha *= opacity;
    if (finalAlpha < 0.000000001) { finalColor = canvas; return; }

    // ── Erase modes ─────────────────────────────────────────────────
    if (eraseMode == 1) {
        // Alpha erase: reduce canvas alpha by brush alpha
        float newA = canvas.a * (1.0 - finalAlpha);
        finalColor = vec4(canvas.rgb, newA);
        return;
    } else if (eraseMode == 2) {
        // Color erase: apply color to semi-transparent regions using gamma formula
        float gamma = 2.2;
        float mask = pow(canvas.a, gamma) * finalAlpha;
        vec3 erasedRGB = mix(canvas.rgb, brushFinal, mask);
        float erasedA = canvas.a * (1.0 - finalAlpha * 0.5);
        finalColor = vec4(erasedRGB, erasedA);
        return;
    }

float cloneOpacity = smudgeStrength;
    if (cloneOpacity > 0.000001) {
        vec2 smudgeUV = clamp(canvasFragUV - smudgeOffsetUV, 0.001, 0.999);
        smudgeUV.y *= -1;
        vec4 smudgeSample = texture(canvasTex, smudgeUV);
        float smudgeA = smudgeSample.a;

// gracefully handle transparent layers to not mix with blacks.
        vec3 smudgeRGB;
        if (smudgeA < 0.0000001)
            smudgeRGB = canvas.rgb;
         else
            smudgeRGB = smudgeSample.rgb;

        vec4 smudgeCol = vec4(smudgeRGB, 1);

        brushFinal = applyBlend(bmidx, smudgeCol, brushFinal, (1.0 - smudgeStrength)*(1.0 - smudgeStrength)).rgb;
        finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);

       // Alpha: treat canvas A as plain channel, lerp smudge alpha into canvas alpha weighted by brush mask
       float blendedA = mix(canvas.a, smudgeA, finalAlpha);

           if (preserveop > 0.5)
            finalColor.a = canvas.a;
           else
            finalColor.a = blendedA;

        return;
    }

    finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
        if (preserveop > 0.5) finalColor.a = canvas.a;
}
