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

void RGBToHSL(Color c, float& h, float& s, float& l) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b));
    float mn = fminf(r, fminf(g, b));
    l = (mx + mn) * 0.5f;
    if (mx == mn) {
        h = 0.0f;
        s = 0.0f;
    } else {
        float d = mx - mn;
        s = (l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
}

void DrawColorGradientAt(Rectangle r, int gradType, float hue) {
    (void)r;
    (void)gradType;
    (void)hue;
}
