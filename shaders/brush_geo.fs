#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float uAngle;
uniform float uSquish;
uniform float uSize;

void main() {
    vec2 uv = fragTexCoord;
    vec2 p = (uv - 0.5) / max(uSize, 0.001);  // scale: bbox->radOut space

    float c = cos(uAngle), s = sin(uAngle);
    vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    r.x /= max(uSquish, 0.01);

    if (abs(r.x) > 0.5 || abs(r.y) > 0.5) {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    vec2 quadUV = r + 0.5;
    finalColor = vec4(quadUV, 0.0, 1.0);
}
