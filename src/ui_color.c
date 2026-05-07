#include "repaint.h"

UISlider sliderHue;
UISlider sliderSat;
UISlider sliderLit;
float colorHue = 0.5f;
float colorSat = 1.0f;
float colorLit = 0.5f;

Color HSLToRGB(float h, float s, float l) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
    float m = l - c * 0.5f;
    float r, g, b;
    int hi = (int)(h * 6.0f);
    switch (hi) {
        case 0: r=c; g=x; b=0; break;
        case 1: r=x; g=c; b=0; break;
        case 2: r=0; g=c; b=x; break;
        case 3: r=0; g=x; b=c; break;
        case 4: r=x; g=0; b=c; break;
        default: r=c; g=0; b=x; break;
    }
    return (Color){
        (unsigned char)((r + m) * 255.0f),
        (unsigned char)((g + m) * 255.0f),
        (unsigned char)((b + m) * 255.0f), 255};
}

void DrawColorGradientAt(Rectangle r, int gradType, float hue) {
    if (gradType == 0) {
        Color stops[] = {
            {255,0,0,255}, {255,255,0,255}, {0,255,0,255},
            {0,255,255,255}, {0,0,255,255}, {255,0,255,255}, {255,0,0,255}
        };
        int n = sizeof(stops)/sizeof(stops[0]) - 1;
        for (int i = 0; i < n; i++) {
            float sx = r.x + (float)i / n * r.width;
            float sw = r.width / n + 1;
            DrawRectangleGradientH((int)sx, (int)r.y, (int)sw, (int)r.height, stops[i], stops[i+1]);
        }
    } else if (gradType == 1) {
        Color c0 = HSLToRGB(hue, 0, colorLit);
        Color c1 = HSLToRGB(hue, 1, colorLit);
        DrawRectangleGradientH((int)r.x, (int)r.y, (int)r.width, (int)r.height, c0, c1);
    } else {
        Color c0 = {0,0,0,255};
        Color c1 = HSLToRGB(hue, colorSat, 0.5f);
        Color c2 = {255,255,255,255};
        DrawRectangleGradientH((int)r.x, (int)r.y, (int)(r.width/2), (int)r.height, c0, c1);
        DrawRectangleGradientH((int)(r.x + r.width/2), (int)r.y, (int)(r.width - r.width/2 + 1), (int)r.height, c1, c2);
    }
}
