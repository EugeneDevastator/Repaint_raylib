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
uniform float uFocalOffset;

// Oblique-cone gradient: returns distance-normalized height (1 = tip, 0 = base edge).
float coneGradient(vec2 p, float R, float shift) {
    float dir = 0.25; // hardcoded direction (rightward)
    vec2 d = vec2(cos(dir * 6.2831853), sin(dir * 6.2831853)) * (shift * R);

    float A = dot(d, d) - R*R;
    float B = -2.0 * (dot(p, d) - R*R);
    float C = dot(p, p) - R*R;

    float t;
    if (abs(A) < 1e-6) {
        t = -C / B;
    } else {
        float disc = max(B*B - 4.0*A*C, 0.0);
        float sq = sqrt(disc);
        float t1 = (-B + sq) / (2.0*A);
        float t2 = (-B - sq) / (2.0*A);
        t = (t1 >= 0.0 && t1 <= 1.0) ? t1 : t2;
    }
    return clamp(t, 0.0, 1.0);
}
float curvePWR = 6.0;
float applyRadialFalloff(float d) {
    if (d < 0.00001) return 0.0;
    float innerT = clamp(uRadIn, 0.0, 0.99);
    float a = 1.0;
    if (d > innerT) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.00001)
            a = 1.0 - (d - innerT) / edgeRange;
		else
		    return 0;
    }
    a = clamp(a, 0.0, 1.0);
    float crvt = uCurve * 2.0 - 1.0;
    float curvePower = (crvt >= 0.0) ? mix(1.0, curvePWR, crvt) : mix(1.0, 1.0/curvePWR, -crvt);
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

    float d;
    if (abs(uFocalOffset) < 0.0001) {
        d = length(r) * 2.0;
    } else {
        float t = coneGradient(r, 0.5, uFocalOffset);
        d = 1.0 - t;
    }
    float alpha = applyRadialFalloff(clamp(d, 0.0, 1.0));

    vec2 quadUV = r + 0.5;
    finalColor = vec4(quadUV, 0.0, alpha);
}
