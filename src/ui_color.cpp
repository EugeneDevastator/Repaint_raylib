#include "repaint.h"

static float hue2rgb(float p, float q, float t) {
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

float colorHue = 0.35f;
float colorSat = 1.0f;
float colorLit = 0.5f;

Color HSLToRGB(float h, float s, float l) {
    float r, g, b;
    if (s == 0.0f) {
        r = g = b = l;
    } else {
        float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        float hk = h;
        float tr = hk + 1.0f / 3.0f;
        float tg = hk;
        float tb = hk - 1.0f / 3.0f;
        if (tr < 0) tr += 1.0f; if (tr > 1.0f) tr -= 1.0f;
        if (tg < 0) tg += 1.0f; if (tg > 1.0f) tg -= 1.0f;
        if (tb < 0) tb += 1.0f; if (tb > 1.0f) tb -= 1.0f;

            r = hue2rgb(p, q, tr);
            g = hue2rgb(p, q, tg);
            b = hue2rgb(p, q, tb);
    }
    return (Color){(unsigned char)(r * 255), (unsigned char)(g * 255), (unsigned char)(b * 255), 255};
}

void DrawColorGradientAt(Rectangle r, int gradType, float hue) {
    (void)r;
    (void)gradType;
    (void)hue;
}
