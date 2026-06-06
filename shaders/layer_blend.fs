#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D underTex;
uniform sampler2D layerTex;
uniform float layerAlpha;
uniform int bmidx;
uniform float layerThreshold;
uniform float layerFeather;

#include blend.glsl

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
