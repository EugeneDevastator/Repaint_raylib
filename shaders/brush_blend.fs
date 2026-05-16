#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec2 localUV;
out vec4 finalColor;

uniform sampler2D texture0;

uniform float radIn;
uniform float radOut;
uniform float opacity;
uniform float x2y;
uniform float resangle;
uniform float useTex;
uniform sampler2D brushTex;

void main() {
    vec4 canvas = texture(texture0, fragTexCoord);

    vec2 p = localUV * 2.0 - 1.0;

    float angle = radians(resangle);
    float c = cos(angle), s = sin(angle);
    vec2 bp;
    bp.x = p.x * c - p.y * s;
    bp.y = p.x * s + p.y * c;
    bp.x /= max(x2y, 0.01);

    float dist = length(bp);
    if (dist > 1.0) {
        finalColor = canvas;
        return;
    }

    float inner = clamp(radIn / max(radOut, 0.001), 0.0, 1.0);
    float alpha = 1.0 - smoothstep(inner, 1.0, dist);
    alpha = clamp(alpha, 0.0, 1.0) * opacity;

    if (alpha < 0.0001) {
        finalColor = canvas;
        return;
    }

    vec2 texUV = bp * 0.5 + 0.5;
    vec3 brushColor;
    if (useTex > 0.5) {
        brushColor = texture(brushTex, texUV).rgb;
    } else {
        brushColor = vec3(texUV, 0.0);
    }
    finalColor.rgb = brushColor.rgb * alpha + canvas.rgb * (1.0 - alpha);
    finalColor.a = alpha + canvas.a * (1.0 - alpha);
}
