#version 330

in vec2 vertexPosition;
in vec2 vertexTexCoord;

out vec2 fragTexCoord;

void main() {
    fragTexCoord = vertexTexCoord;
    gl_Position = vec4(vertexPosition, 0.0, 1.0);
}
