#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D geoTex;
uniform sampler2D dstTex;
uniform sampler2D brushTex;

uniform float opacity;
uniform float sol;
uniform float sol2op;
uniform float seed;
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;  // pixel-space offset
uniform float texBlendVal;
uniform float texScale;
uniform vec2  texOffset;
uniform vec2  userTexOrigin;
uniform float texFeather;
uniform float texThresh;
uniform int   texNoisemode;
uniform bool  useLumAsAlpha;
uniform bool  hasTexture;
uniform int   texColorMode;
uniform int   bmidx;
uniform vec4  brushColor;
uniform float radOut;
uniform vec2  stampCenter;
uniform vec2  canvasSize;
uniform float pwr;
uniform int   eraseMode;
uniform bool  uSeamless;
uniform bool  uPixelPerfect;
in vec2 canvasFragUV;
in vec2 outCanvasPx;
uniform vec2 blitSize;    // floor(stampSizePx) — pixel-aligned stamp size
uniform vec2 fracShift;   // x0-floor(x0), y0-floor(y0) — sub-pixel offset [0,1)
#include blend.glsl

float applyThreshold(float combined, float cut, float texFeather) {
    float edgeDist = combined - cut;
    if (texFeather <= 0.0) return (edgeDist > 0.0) ? 1.0 : 0.0;
    return clamp(edgeDist / max(texFeather, 0.0001), 0.0, 1.0);
}

void main() {
    vec2 uv       = fragTexCoord;
    vec2 sampleUV = vec2(
        uv.x - fracShift.x / blitSize.x,
        1.0 - uv.y - fracShift.y / blitSize.y
    );
    vec4 geouv = texture(geoTex, sampleUV);

    // Canvas read: floor matches blit's floor(x0/y0) — consistent alignment
    int cpx = int(floor(outCanvasPx.x));
    int cpy = int(floor(outCanvasPx.y));
    if (uSeamless) {
        cpx = int(mod(float(cpx), canvasSize.x));
        cpy = int(mod(float(cpy), canvasSize.y));
    }
    cpx = clamp(cpx, 0, int(canvasSize.x) - 1);
    cpy = clamp(cpy, 0, int(canvasSize.y) - 1);
    cpy = int(canvasSize.y) - 1 - cpy;
    vec4 canvas = texelFetch(dstTex, ivec2(cpx, cpy), 0);

    if (geouv.a < 0.01) {
        finalColor = canvas;
        return;
    }

    float alpha = geouv.a;
    float finalAlpha;
    vec3 brushFinal;

    if (hasTexture) {
        vec2 stUV = geouv.rg;
        if (texNoisemode == 0) {
            stUV = (canvasFragUV - 0.5) * canvasSize / 256.0 * texScale + userTexOrigin;
        } else if (texNoisemode == 1) {
            stUV = geouv.rg * texScale + texOffset;
        } else {
            stUV = (geouv.rg - 0.5) * texScale + userTexOrigin;
        }
        vec4 texel = texture(brushTex, stUV);
        float userTexA = useLumAsAlpha
            ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
            : texel.a;
        if (texThresh < 0.0) userTexA = 1.0 - userTexA;
        finalAlpha = clamp(applyThreshold(alpha * userTexA, abs(texThresh), texFeather), 0.0, 1.0);
        if (texColorMode == 0) {
            brushFinal = brushColor.rgb;
        } else if (texColorMode == 1) {
            brushFinal = texel.rgb;
        } else if (texColorMode == 2) {
            brushFinal = texel.rgb * brushColor.rgb;
        } else {
            float lum = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
            if (lum < 0.5)
                brushFinal = mix(vec3(0.0), brushColor.rgb, lum * 2.0);
            else
                brushFinal = mix(brushColor.rgb, vec3(1.0), (lum - 0.5) * 2.0);
        }
    } else {
        finalAlpha = alpha;
        brushFinal = brushColor.rgb;
    }

    finalAlpha *= opacity;
    if (finalAlpha < 0.000000001) { finalColor = canvas; return; }

    if (eraseMode == 1) {
        finalColor = vec4(canvas.rgb, canvas.a * (1.0 - finalAlpha));
        return;
    } else if (eraseMode == 2) {
        float mask = canvas.a * finalAlpha;
        vec3 erasedRGB = mix(canvas.rgb, brushFinal, mask);
        finalColor = vec4(erasedRGB, canvas.a * (1.0 - finalAlpha * 0.5));
        return;
    }

float cloneOpacity = smudgeStrength;
    if (cloneOpacity > 0.000001) {
        vec2 srcPx = outCanvasPx - smudgeOffsetUV;
        if (uSeamless)
            srcPx = mod(srcPx, canvasSize);
        else
            srcPx = clamp(srcPx, vec2(0.0), canvasSize - vec2(1.0));

        vec4 smudge;
        if (uPixelPerfect) {
            ivec2 pp = ivec2(clamp(srcPx, vec2(0.0), canvasSize - vec2(1.0)));
            pp.y = int(canvasSize.y) - 1 - pp.y;
            smudge = texelFetch(dstTex, pp, 0);
        } else {
			// must use linear blending, at least its algo works best here.
            smudge = sampleBilinear(dstTex, vec2(srcPx.x, canvasSize.y - srcPx.y), canvasSize, bmidx, uSeamless);
        }

        vec3 smudgeRGB = smudge.rgb;
        float smudgeA  = smudge.a;
        finalColor = applyBlend(bmidx, canvas, smudgeRGB, finalAlpha);
		//finalColor.rgb = (smudge.rgb);
			   finalColor.a =1;
			   return;
        if (smudgeA < 0.0000001) smudgeRGB = canvas.rgb;
        brushFinal = smudgeRGB;



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
