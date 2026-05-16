#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float angle;
uniform float x2y;

void main() {
    vec2 uv = fragTexCoord;
    vec2 p = uv - 0.5;

    float c = cos(angle), s = sin(angle);
    vec2 r;
    r.x = c * p.x - s * p.y;
    r.y = s * p.x + c * p.y;

    r.x /= max(x2y, 0.01);

    if (abs(r.x) > 0.5 || abs(r.y) > 0.5) {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec2 quadUV = r + 0.5;
    finalColor = vec4(quadUV, 0.0, 1.0);
}
