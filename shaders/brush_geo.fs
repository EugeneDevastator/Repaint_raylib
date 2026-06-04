#version 330
//brush geo fs
in vec2 fragTexCoord;
out vec4 finalColor;

uniform float uAngle;
uniform float uSquish;
uniform float uSize;
uniform float uPerspective;
uniform float uRadIn;
uniform float uCurve;

float applyRadialFalloff(float d) {
    if (d < 0.00000001) return 0.0;
    float innerT = clamp(uRadIn, 0.0, 1.0);
    float a = 1.0;
    if (d > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.001)
            a = 1.0 - (d - innerT) / edgeRange;
    }
    a = clamp(a, 0.0, 1.0);
    float crvt = uCurve * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, 3.0, crvt) : mix(1.0, 1.0/3.0, -crvt);
    return clamp(pow(a, curvePower), 0.0, 1.0);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 p = (uv - 0.5) / max(uSize, 0.001);

    // Perspective distortion: trapezoidal effect
    float persp = uPerspective;
    float widthAtY = 1.0 - persp * (p.y + 0.5);
    widthAtY = max(widthAtY, 0.01);

    if (abs(p.x) > 0.5 * widthAtY) {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    p.x /= widthAtY;

    float c = cos(uAngle), s = sin(uAngle);
    vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
    r.x /= max(uSquish, 0.01);

    if (abs(r.x) > 0.5 || abs(r.y) > 0.5) {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float d = length(r) * 2.0;
    float alpha = applyRadialFalloff(d);

    vec2 quadUV = r + 0.5;
    finalColor = vec4(quadUV, 0.0, alpha);
}
