#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D dstTex;
uniform sampler2D brushTex;

uniform float uAngle;
uniform float uSquish;
uniform float uSize;
uniform float uPerspective;
uniform float uRadIn;
uniform float uCurve;
uniform float uFocalOffset;

uniform float opacity;
uniform float sol;
uniform float sol2op;
uniform float seed;
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;
uniform float smudgeSrcRad;
uniform float smudgeAngleDelta;
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
uniform int   texTiling;
uniform int   bmidx;
uniform vec4  brushColor;
uniform float radOut;
uniform vec2  stampCenter;
uniform vec2  canvasSize;
uniform float pwr;
uniform bool  uSeamless;
uniform bool  uPixelPerfect;
in vec2 canvasFragUV;
in vec2 outCanvasPx;
uniform vec2 blitSize;
uniform vec2 fracShift;
#include blend.glsl

float coneGradient(vec2 p, float R, float shift) {
    float dir = 0.25;
    vec2 d = vec2(cos(dir * 6.2831853), sin(dir * 6.2831853)) * (shift * R);
    float A = dot(d, d) - R*R;
    float B = -2.0 * (dot(p, d) - R*R);
    float C = dot(p, p) - R*R;
    float t;
    if (abs(A) < 1e-6) {
        t = -C / B;
    } else {
        float disc = max(B*B - 4.0*A*C, 0.0);
        float sq = sqrt(disc);
        float t1 = (-B + sq) / (2.0*A);
        float t2 = (-B - sq) / (2.0*A);
        if (t1 >= 0.0 && t1 <= 1.0)
            t = t1;
        else if (t2 >= 0.0 && t2 <= 1.0)
            t = t2;
        else
            t = (C > 0.0) ? 0.0 : 1.0;
    }
    return clamp(t, 0.0, 1.0);
}

float applyThreshold(float combined, float cut, float texFeather) {
    float edgeDist = combined - cut;
    if (texFeather <= 0.0) return (edgeDist > 0.0) ? 1.0 : 0.0;
    return clamp(edgeDist / max(texFeather, 0.0001), 0.0, 1.0);
}

float curvePWR = 6.0;
float applyRadialFalloff(float d) {
    if (d < 0.00001) return 1.0;

    float innerT = clamp(uRadIn, 0.0, 0.99);
    float a = 1.0;
    if (d > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.00001)
            a = 1.0 - (d - innerT) / edgeRange;
        else
            return 0;
    }
    a = clamp(a, 0.0, 1.0);
    float crvt = uCurve * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, curvePWR, crvt) : mix(1.0, 1.0/curvePWR, -crvt);
    return clamp(pow(a, curvePower), 0.0, 1.0);
}

vec4 readCanvas(ivec2 px) {
    if (uSeamless) {
        px.x = int(mod(float(px.x), canvasSize.x));
        px.y = int(mod(float(px.y), canvasSize.y));
    }
    px.x = clamp(px.x, 0, int(canvasSize.x) - 1);
    px.y = clamp(px.y, 0, int(canvasSize.y) - 1);
    px.y = int(canvasSize.y) - 1 - px.y;
    return texelFetch(dstTex, px, 0);
}

void main() {
    vec2 uv = vec2(
        fragTexCoord.x - fracShift.x / blitSize.x,
        fragTexCoord.y + fracShift.y / blitSize.y
    );
    vec2 p = (uv - 0.5) / max(uSize, 0.001);

    float persp = uPerspective;
    float widthAtY = 1.0 - persp * (p.y + 0.5);
    widthAtY = max(widthAtY, 0.01);

    if (abs(p.x) > 0.5 * widthAtY) {
        finalColor = readCanvas(ivec2(floor(outCanvasPx.x), floor(outCanvasPx.y)));
        return;
    }

    p.x /= widthAtY;

    float c = cos(uAngle), s = sin(uAngle);
    vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    r.x /= max(uSquish, 0.01);

    if (abs(r.x) > 0.5 || abs(r.y) > 0.5) {
        finalColor = readCanvas(ivec2(floor(outCanvasPx.x), floor(outCanvasPx.y)));
        return;
    }

    float d;
    if (abs(uFocalOffset) < 0.0001) {
        d = length(r) * 2.0;
    } else {
        float t = coneGradient(r, 0.5, uFocalOffset);
        d = 1.0 - t;
    }
    float alpha = applyRadialFalloff(clamp(d, 0.0, 1.0));

    vec4 canvas = readCanvas(ivec2(floor(outCanvasPx.x), floor(outCanvasPx.y)));

    if (alpha < 0.01) {
        finalColor = canvas;
        return;
    }

    float finalAlpha;
    vec3 brushFinal;
    vec2 quadUV = r + 0.5;

    if (hasTexture) {
        vec2 stUV = quadUV;
        if (texNoisemode == 0) {
            stUV = (canvasFragUV - 0.5) * canvasSize / 256.0 * texScale + userTexOrigin;
        } else if (texNoisemode == 1) {
            stUV = (quadUV - 0.5) * texScale + userTexOrigin + texOffset;
        } else {
            stUV = (quadUV - 0.5) * texScale + userTexOrigin;
        }
        vec4 texel = texture(brushTex, stUV);
        if (texTiling == 1 && (stUV.x < 0.0 || stUV.x > 1.0 || stUV.y < 0.0 || stUV.y > 1.0))
            texel.a = 0.0;
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

    float cloneOpacity = smudgeStrength;
    if (cloneOpacity > 0.000001) {
        vec2 stampPosPx = vec2(stampCenter.x * canvasSize.x, (1.0 - stampCenter.y) * canvasSize.y);
        vec2 rel = (outCanvasPx - stampPosPx) * (smudgeSrcRad / radOut);
        float a = smudgeAngleDelta;
        float ca = cos(a), sa = sin(a);
        vec2 rot = vec2(rel.x*ca - rel.y*sa, rel.x*sa + rel.y*ca);
        vec2 srcPx = (stampPosPx - smudgeOffsetUV) + rot;
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
            smudge = sampleBilinear(dstTex, vec2(srcPx.x, canvasSize.y - srcPx.y), canvasSize, bmidx, uSeamless);
        }

        vec3 smudgeRGB = smudge.rgb;
        float smudgeA  = smudge.a;
        if (smudgeA < 0.0000001) smudgeRGB = canvas.rgb;
        brushFinal = applyBlend(bmidx, smudge, brushFinal, (1.0 - smudgeStrength)*(1.0 - smudgeStrength)).rgb;
        finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);

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
