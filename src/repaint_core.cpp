#include "repaint.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

float PackedFloat_GetVal(PackedFloat* pf) {
    return (float)(pf->IntVal + pf->FVal / 255.0f);
}

void PackedFloat_SetVal(PackedFloat* pf, double val) {
    pf->IntVal = (int16_t)floor(val);
    pf->FVal = (uint8_t)floor((val - floor(val)) * 255.0f);
}

float Dist2D(Vector2 pos1, Vector2 pos2) {
    float dx = pos1.x - pos2.x;
    float dy = pos1.y - pos2.y;
    return sqrtf(dx * dx + dy * dy);
}

float AtanXY(float x, float y) {
    double ang;
    int sg = -1;
    if (y > 0) sg = 0;
    if (x == 0 && y == 0) ang = 0;
    else if (x == 0) ang = PI / 2.0 + (sg + 1) / 2.0 * PI;
    else if (x > 0 && y < 0) ang = 2.0 * PI + atanf(y / x);
    else if (x < 0) ang = PI + atanf(y / x);
    else ang = atanf(y / x);
    return (float)(ang - PI);
}

float RngConv(float inval, float inmin, float inmax, float outmin, float outmax) {
    float inrange = inmax - inmin;
    float inrel = (inval - inmin) / inrange;
    inrel = fmaxf(inrel, 0.0f);
    inrel = fminf(inrel, 1.0f);
    float outrange = outmax - outmin;
    return outrange * inrel + outmin;
}

// ── Canvas_NewDocument ─────────────────────────────────────────────
// Create an empty document with zero layers.
// The caller must call LayerStack_Init + LayerStack_Bind + LayerStack_AddNew
// to set up GPU resources and add the initial white background layer.
Canvas Canvas_NewDocument(int width, int height) {
    Canvas canvas;
    canvas.width = width;
    canvas.height = height;
    canvas.backgroundColor = WHITE;
    canvas.layerCount = 0;
    canvas.layerImages = NULL;
    canvas.layerProps = NULL;
    return canvas;
}

void Canvas_Destroy(Canvas* canvas) {
    for (int i = 0; i < canvas->layerCount; i++)
        UnloadImage(canvas->layerImages[i]);
    if (canvas->layerImages) { free(canvas->layerImages); canvas->layerImages = NULL; }
    if (canvas->layerProps)  { free(canvas->layerProps);  canvas->layerProps = NULL; }
    canvas->layerCount = 0;
}

void Canvas_SetLayerOpacity(Canvas* canvas, int layer, float op) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].op = op;
}
void Canvas_SetLayerBlendMode(Canvas* canvas, int layer, int bm) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].blendmode = bm;
}
void Canvas_SetLayerVisible(Canvas* canvas, int layer, bool visible) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].visible = visible;
}
void Canvas_SetLayerThreshold(Canvas* canvas, int layer, float threshold) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].threshold = threshold;
}
void Canvas_SetLayerFeather(Canvas* canvas, int layer, float feather) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].feather = feather;
}
