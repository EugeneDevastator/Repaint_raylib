#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fragTexCoord;

uniform mat4 mvp;
uniform float angle;
uniform float x2y;  // 0=circle, 1=flat line

void main() {
    vec2 local = vertexTexCoord * 2.0 - 1.0;
    float c = cos(angle);
    float s = sin(angle);
    // rotate first
    float rx = local.x * c - local.y * s;
    float ry = local.x * s + local.y * c;
    // squish Y: x2y=0 -> scaleY=1, x2y=1 -> scaleY=0.01
    float scaleY = mix(1.0, 0.01, x2y);
    float px = rx;
    float py = ry / max(scaleY, 0.01);
    fragTexCoord = vec2(px, py) * 0.5 + 0.5;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
