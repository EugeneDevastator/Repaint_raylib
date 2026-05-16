#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec2 localUV;

uniform mat4 mvp;
uniform vec4 rectBounds;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    localUV = (vertexTexCoord - rectBounds.xy) / max(rectBounds.zw, vec2(0.000001));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
