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
float applytr(float inp, float range, float maxv) {
    float minv = clamp(maxv - range, 0.0, 1.0);
    float span = maxv - minv;

    if(span < 0.0001) return inp > maxv ? 1.0 : 0.0;

    return clamp((inp - minv) / span, 0.0, 1.0);
}
void main() {
    vec4 underLayer = texture(underTex, fragTexCoord);
    vec4 thisLayer  = texture(layerTex, fract(fragTexCoord));
    float layerA    = thisLayer.a;

	layerA = applytr(layerA, layerFeather, 1-layerThreshold);

    if (layerA < 0.0001) {
		finalColor = underLayer; return;
	}
	layerA     *= layerAlpha;
    finalColor = applyBlend(bmidx, underLayer, thisLayer.rgb, layerA);

}
