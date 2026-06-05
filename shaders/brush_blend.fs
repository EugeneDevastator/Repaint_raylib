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
in vec2 canvasFragUV;

vec3 rgbToOklab(vec3 c) {
    vec3 lin = c * c;
    float l = 0.4122214708 * lin.r + 0.5363325363 * lin.g + 0.0514459929 * lin.b;
    float m = 0.2119034982 * lin.r + 0.6806995451 * lin.g + 0.1073969566 * lin.b;
    float s = 0.0883024619 * lin.r + 0.2817188376 * lin.g + 0.6299787005 * lin.b;
    float l_ = pow(max(l, 0.0), 1.0/3.0);
    float m_ = pow(max(m, 0.0), 1.0/3.0);
    float s_ = pow(max(s, 0.0), 1.0/3.0);
    return vec3(
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    );
}

vec3 oklabToRgb(vec3 lab) {
    float l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    vec3 lin = vec3(
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    );
    return sqrt(max(lin, 0.0));
}

float applyThreshold(float combined, float cut, float texFeather) {
    float edgeDist = combined - cut;
    if (texFeather <= 0.0) return (edgeDist > 0.0) ? 1.0 : 0.0;
    return clamp(edgeDist / max(texFeather, 0.0001), 0.0, 1.0);
}

vec3 rgb2hsl(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    float d = mx - mn;
    float h = 0.0, s = 0.0, l = (mx + mn) * 0.5;
    if (d > 0.001) {
        s = (l <= 0.5) ? d / (mx + mn) : d / (2.0 - mx - mn);
        if (c.r == mx)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (c.g == mx) h = (c.b - c.r) / d + 2.0;
        else                h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0; if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s < 0.001) return vec3(l);
    float q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p, q, h + 1.0/3.0), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0/3.0));
}

vec4 applyBlend(int mode, vec4 canvas, vec3 brushRGB, float brushA) {
    vec3 brushPremul = brushRGB * brushA;
    vec3 outRGB;
    float outA;
    if (canvas.a <= 0.00000001) {
        if (mode == 2 || mode == 3) return vec4(canvas.rgb, 0.0);
        return vec4(clamp(brushRGB, 0.0, 1.0), clamp(brushA, 0.0, 1.0));
    }

if (mode == 0) { // N-OKLab
    vec3 brushLab  = rgbToOklab(brushRGB);
    vec3 canvasLab = rgbToOklab(canvas.rgb);
    float wBrush  = brushA;
    float wCanvas = canvas.a * (1.0 - brushA);
    float wTotal  = wBrush + wCanvas;
    vec3 blendedLab;
    if (wTotal > 0.00001) blendedLab = (brushLab * wBrush + canvasLab * wCanvas) / wTotal;
    else blendedLab = brushLab;
    outRGB = oklabToRgb(blendedLab);
    outA   = wTotal;
} else if (mode == 1) { // N-Linear
    vec3 brushPremul  = brushRGB * brushA;
    vec3 canvasPremul = canvas.rgb * canvas.a;
    outA   = brushA + canvas.a * (1.0 - brushA);
    outRGB = (outA > 0.00001)
        ? (brushPremul + canvasPremul * (1.0 - brushA)) / outA
        : brushRGB;
} else if (mode == 2) {
    outRGB = canvas.rgb; outA = canvas.a * (1.0 - brushA);
} else if (mode == 3) {
    float eraseMask = canvas.a * brushA;
    outRGB = mix(canvas.rgb, brushRGB, eraseMask);
    outA   = canvas.a * (1.0 - brushA * 0.5);
} else if (mode == 4) {
    outRGB = 1.0 - (1.0 - canvas.rgb) * (1.0 - brushPremul);
    outA   = 1.0 - (1.0 - canvas.a) * (1.0 - brushA);
} else if (mode == 5) {
    outRGB = canvas.rgb + brushPremul * (1.0 - canvas.rgb);
    outA   = min(1.0, canvas.a + brushA);
} else if (mode == 6) {
    outRGB = max(canvas.rgb, brushPremul);
    outA   = max(canvas.a, brushA);
} else if (mode == 7) {
    outRGB = min(canvas.rgb, brushPremul);
    outA   = min(canvas.a, brushA);
} else if (mode == 8) {
    outRGB = 1.0 - (1.0 - canvas.rgb) / (brushPremul + 0.001);
    outA   = min(1.0, canvas.a + brushA);
} else if (mode == 9) {
    outRGB = canvas.rgb * mix(vec3(1.0), brushRGB, brushA);
    outA   = canvas.a;
} else if (mode == 10) {
    vec3 ov;
    if (canvas.rgb.r < 0.5) ov.r = 2.0 * canvas.rgb.r * brushRGB.r;
    else ov.r = 1.0 - 2.0 * (1.0 - canvas.rgb.r) * (1.0 - brushRGB.r);
    if (canvas.rgb.g < 0.5) ov.g = 2.0 * canvas.rgb.g * brushRGB.g;
    else ov.g = 1.0 - 2.0 * (1.0 - canvas.rgb.g) * (1.0 - brushRGB.g);
    if (canvas.rgb.b < 0.5) ov.b = 2.0 * canvas.rgb.b * brushRGB.b;
    else ov.b = 1.0 - 2.0 * (1.0 - canvas.rgb.b) * (1.0 - brushRGB.b);
    outRGB = canvas.rgb * (1.0 - brushA) + ov * brushA;
    outA   = brushA + canvas.a * (1.0 - brushA);
} else if (mode == 11) {
    vec3 bHSL = rgb2hsl(brushRGB);
    vec3 cHSL = rgb2hsl(canvas.rgb);
    vec3 col = hsl2rgb(vec3(bHSL.x, bHSL.y, cHSL.z));
    outRGB = canvas.rgb * (1.0 - brushA) + col * brushA;
    outA   = brushA + canvas.a * (1.0 - brushA);
} else {
    outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
    outA   = brushA + canvas.a * (1.0 - brushA);
}

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec2 uv       = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);
    vec4 geouv    = texture(geoTex, sampleUV);

    vec4 canvas = texture(dstTex, canvasFragUV);

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

        vec2 smudgeUV = canvasFragUV;
        smudgeUV.x -= smudgeOffsetUV.x / canvasSize.x;
        smudgeUV.y += smudgeOffsetUV.y / canvasSize.y;

        vec4 smudgeSample = texture(dstTex, smudgeUV);
        float smudgeA = smudgeSample.a;

        vec3 smudgeRGB;
        if (smudgeA < 0.0000001)
            smudgeRGB = canvas.rgb;
         else
            smudgeRGB = smudgeSample.rgb;

        brushFinal = smudgeRGB;
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
