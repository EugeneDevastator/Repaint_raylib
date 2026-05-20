#version 330
//brush geo fs
in vec2 fragTexCoord;
out vec4 finalColor;

uniform float uAngle;
uniform float uSquish;
uniform float uSize;
uniform float uPerspective;

void main() {
    vec2 uv = fragTexCoord;
    vec2 p = (uv - 0.5) / max(uSize, 0.001);

    // Perspective distortion: trapezoidal effect
    // Top edge (p.y = -0.5) stays full width, bottom edge (p.y = 0.5) shrinks
    float persp = uPerspective;
    float widthAtY = 1.0 - persp * (p.y + 0.5); // 1.0 at top, (1-persp) at bottom
    widthAtY = max(widthAtY, 0.01);

    // Check if p.x is within the trapezoid bounds at this Y
    if (abs(p.x) > 0.5 * widthAtY) {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Normalize p.x to [-0.5, 0.5] range for UV output
    p.x /= widthAtY;

    // Apply rotation and squish
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
