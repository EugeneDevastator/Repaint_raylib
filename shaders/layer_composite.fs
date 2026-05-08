#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D accumTex;
uniform sampler2D layerTex;
uniform float layerAlpha;

void main() {
    vec4 accum = texture(accumTex, fragTexCoord);
    vec4 layer = texture(layerTex, fragTexCoord);

    // Straight alpha → premultiplied, with layer opacity applied
    float a   = layer.a * layerAlpha;
    vec3 rgb  = layer.rgb * a;          // premultiply by COMBINED alpha

    // Premultiplied "over"
    vec3 outRGB = rgb + accum.rgb * (1.0 - a);
    float outA  = a   + accum.a   * (1.0 - a);

    finalColor = vec4(outRGB, outA);
}
