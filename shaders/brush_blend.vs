#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec2 localUV;

uniform mat4 mvp;
uniform vec2 stampCenter;
uniform vec2 stampHalf;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    vec2 ndc = (vertexTexCoord - stampCenter) / max(stampHalf, vec2(0.000001));
    localUV = ndc;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
