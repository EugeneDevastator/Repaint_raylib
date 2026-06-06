#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D underTex;
uniform sampler2D layerTex;
uniform float layerAlpha;
uniform int bmidx;
uniform float layerThreshold;
uniform float layerFeather;

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
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
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

vec4 applyBlend(int mode, vec4 underLayer, vec3 layerRGB, float layerA) {
    vec3 layerPremul = layerRGB * layerA;
    vec3 outRGB;
    float outA;

    if (underLayer.a <= 0.01) {
        outRGB = layerRGB;
        outA   = layerA;
        return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
    }

    if (mode == 0) { // N-Gamma
        vec3 layerLin = layerRGB * layerRGB;
        vec3 underLin = underLayer.rgb * underLayer.rgb;
        outRGB = sqrt(layerLin * layerA + underLin * (1.0 - layerA));
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else if (mode == 1) { // N-Linear
        outRGB = layerPremul + underLayer.rgb * (1.0 - layerA);
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else if (mode == 2) { // N-OKLab
        vec3 layerLab = rgbToOklab(layerRGB);
        vec3 underLab = rgbToOklab(underLayer.rgb);
        float wLayer  = layerA;
        float wUnder  = underLayer.a * (1.0 - layerA);
        float wTotal  = wLayer + wUnder;
        vec3 blendedLab;
        if (wTotal > 0.00001) blendedLab = (layerLab * wLayer + underLab * wUnder) / wTotal;
        else blendedLab = layerLab;
        outRGB = oklabToRgb(blendedLab);
        outA   = wTotal;
    } else if (mode == 3) { // Screen
        outRGB = 1.0 - (1.0 - underLayer.rgb) * (1.0 - layerPremul);
        outA   = 1.0 - (1.0 - underLayer.a) * (1.0 - layerA);
    } else if (mode == 4) { // Color Dodge
        outRGB = underLayer.rgb + layerPremul * (1.0 - underLayer.rgb);
        outA   = min(1.0, underLayer.a + layerA);
    } else if (mode == 5) { // Lighten
        outRGB = max(underLayer.rgb, layerPremul);
        outA   = max(underLayer.a, layerA);
    } else if (mode == 6) { // Darken
        outRGB = min(underLayer.rgb, layerPremul);
        outA   = min(underLayer.a, layerA);
    } else if (mode == 7) { // Burn
        outRGB = 1.0 - (1.0 - underLayer.rgb) / (layerPremul + 0.001);
        outA   = min(1.0, underLayer.a + layerA);
    } else if (mode == 8) { // Multiply
        outRGB = underLayer.rgb * mix(vec3(1.0), layerRGB, layerA);
        outA   = underLayer.a;
    } else if (mode == 9) { // Overlay
        vec3 ov;
        if (underLayer.rgb.r < 0.5) ov.r = 2.0 * underLayer.rgb.r * layerRGB.r;
        else ov.r = 1.0 - 2.0 * (1.0 - underLayer.rgb.r) * (1.0 - layerRGB.r);
        if (underLayer.rgb.g < 0.5) ov.g = 2.0 * underLayer.rgb.g * layerRGB.g;
        else ov.g = 1.0 - 2.0 * (1.0 - underLayer.rgb.g) * (1.0 - layerRGB.g);
        if (underLayer.rgb.b < 0.5) ov.b = 2.0 * underLayer.rgb.b * layerRGB.b;
        else ov.b = 1.0 - 2.0 * (1.0 - underLayer.rgb.b) * (1.0 - layerRGB.b);
        outRGB = underLayer.rgb * (1.0 - layerA) + ov * layerA;
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else if (mode == 10) { // Color
        vec3 lHSL = rgb2hsl(layerRGB);
        vec3 uHSL = rgb2hsl(underLayer.rgb);
        vec3 col = hsl2rgb(vec3(lHSL.x, lHSL.y, uHSL.z));
        outRGB = underLayer.rgb * (1.0 - layerA) + col * layerA;
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else {
        outRGB = layerPremul + underLayer.rgb * (1.0 - layerA);
        outA   = layerA + underLayer.a * (1.0 - layerA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec4 underLayer = texture(underTex, fragTexCoord);
    vec4 thisLayer  = texture(layerTex, fract(fragTexCoord));
    float layerA    = thisLayer.a;

    // Apply threshold and feather
    if (layerThreshold > 0.0 || layerFeather < 1.0) {
        float t = layerThreshold;
        float f = max(layerFeather, 0.001);
        float edgeDist = layerA - t;
        layerA = clamp(edgeDist / f, 0.0, 1.0);
    }

    if (layerA < 0.0001) { finalColor = underLayer; return; }
			  layerA     *= layerAlpha;
    finalColor = applyBlend(bmidx, underLayer, thisLayer.rgb, layerA);

}
