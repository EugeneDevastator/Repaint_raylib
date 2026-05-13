#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D layerTex;
uniform float layerAlpha;
uniform int bmidx;

vec4 applyBlend(int mode, vec4 underLayer, vec3 layerRGB, float layerA) {
    vec3 layerPremul = layerRGB * layerA;
    vec3 outRGB;
    float outA;

    if (underLayer.a <= 0.01) {
        outRGB = layerRGB;
        outA   = layerA;
        return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
    }

    if (mode == 0) {
        vec3 layerLin = layerRGB * layerRGB;
        vec3 underLin = underLayer.rgb * underLayer.rgb;
        outRGB = sqrt(layerLin * layerA + underLin * (1.0 - layerA));
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else if (mode == 1) {
        outRGB = underLayer.rgb + layerPremul;
        outA   = min(1.0, underLayer.a + layerA);
    } else if (mode == 2) {
        outRGB = underLayer.rgb + layerPremul * (1.0 - underLayer.rgb);
        outA   = min(1.0, underLayer.a + layerA);
    } else if (mode == 3) {
        outRGB = 1.0 - (1.0 - underLayer.rgb) * (1.0 - layerPremul);
        outA   = 1.0 - (1.0 - underLayer.a) * (1.0 - layerA);
    } else if (mode == 4) {
        outRGB = max(underLayer.rgb, layerPremul);
        outA   = max(underLayer.a, layerA);
    } else if (mode == 5) {
        outRGB = 1.0 - (1.0 - underLayer.rgb) / (layerPremul + 0.001);
        outA   = min(1.0, underLayer.a + layerA);
    } else if (mode == 6) {
        outRGB = underLayer.rgb * mix(vec3(1.0), layerRGB, layerA);
        outA   = underLayer.a;
    } else if (mode == 7) {
        outRGB = min(underLayer.rgb, layerPremul);
        outA   = min(underLayer.a, layerA);
    } else if (mode == 8) {
        outRGB = layerPremul + underLayer.rgb * (1.0 - layerA);
        outA   = layerA + underLayer.a * (1.0 - layerA);
    } else {
        outRGB = layerPremul + underLayer.rgb * (1.0 - layerA);
        outA   = layerA + underLayer.a * (1.0 - layerA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

void main() {
    vec4 underLayer = texture(texture0, fragTexCoord);
    vec4 thisLayer  = texture(layerTex, fragTexCoord);
    float layerA    = thisLayer.a * layerAlpha;

    if (layerA < 0.001) { finalColor = underLayer; return; }

    finalColor = applyBlend(bmidx, underLayer, thisLayer.rgb, layerA);
}
