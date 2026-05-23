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

Canvas Canvas_Create(int width, int height, Color bgColor) {
    Canvas canvas;
    canvas.width = width;
    canvas.height = height;
    canvas.backgroundColor = bgColor;
    canvas.layerCount = 0;
    canvas.layerImages = NULL;
    canvas.layerProps = NULL;
    Canvas_AddLayer(&canvas);
    // First layer is the canvas background — fill with white instead of transparent
    UnloadImage(canvas.layerImages[0]);
    canvas.layerImages[0] = GenImageColor(width, height, WHITE);
    return canvas;
}

void Canvas_Destroy(Canvas* canvas) {
    for (int i = 0; i < canvas->layerCount; i++) {
        UnloadImage(canvas->layerImages[i]);
    }
    if (canvas->layerImages) { free(canvas->layerImages); canvas->layerImages = NULL; }
    if (canvas->layerProps)  { free(canvas->layerProps);  canvas->layerProps = NULL; }
    canvas->layerCount = 0;
}

void Canvas_AddLayer(Canvas* canvas) {
    Canvas_InsertLayer(canvas, canvas->layerCount);
}

void Canvas_InsertLayer(Canvas* canvas, int idx) {
    if (idx < 0) idx = 0;
    if (idx > canvas->layerCount) idx = canvas->layerCount;
    canvas->layerCount++;
    canvas->layerImages = (Image*)realloc(canvas->layerImages, canvas->layerCount * sizeof(Image));
    canvas->layerProps = (sLayerProps*)realloc(canvas->layerProps, canvas->layerCount * sizeof(sLayerProps));
    for (int i = canvas->layerCount - 1; i > idx; i--) {
        canvas->layerImages[i] = canvas->layerImages[i - 1];
        canvas->layerProps[i] = canvas->layerProps[i - 1];
    }
    canvas->layerImages[idx] = GenImageColor(canvas->width, canvas->height, BLANK);
    ImageFormat(&canvas->layerImages[idx], PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    canvas->layerProps[idx].op = 1.0f;
    canvas->layerProps[idx].visible = true;
    canvas->layerProps[idx].blendmode = bmGamma;
    canvas->layerProps[idx].presop = 0;
    canvas->layerProps[idx].locked = false;
    canvas->layerProps[idx].threshold = 0.0f;
    canvas->layerProps[idx].feather = 1.0f;
    canvas->layerProps[idx].layerName[0] = '\0';
    canvas->layerProps[idx].mat[0] = 1.0f; canvas->layerProps[idx].mat[1] = 0.0f; canvas->layerProps[idx].mat[2] = 0.0f;
    canvas->layerProps[idx].mat[3] = 0.0f; canvas->layerProps[idx].mat[4] = 1.0f; canvas->layerProps[idx].mat[5] = 0.0f;
}

void Canvas_DeleteLayer(Canvas* canvas, int index) {
    if (index < 0 || index >= canvas->layerCount || canvas->layerCount <= 1) return;
    UnloadImage(canvas->layerImages[index]);
    for (int i = index; i < canvas->layerCount - 1; i++) {
        canvas->layerImages[i] = canvas->layerImages[i + 1];
        canvas->layerProps[i] = canvas->layerProps[i + 1];
    }
    canvas->layerCount--;
    canvas->layerImages = (Image*)realloc(canvas->layerImages, canvas->layerCount * sizeof(Image));
    canvas->layerProps = (sLayerProps*)realloc(canvas->layerProps, canvas->layerCount * sizeof(sLayerProps));
}

void Canvas_SetLayerOpacity(Canvas* canvas, int layer, float op) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].op = op;
}

void Canvas_SetLayerBlendMode(Canvas* canvas, int layer, int bm) {
    if (layer >= 0 && layer < canvas->layerCount) canvas->layerProps[layer].blendmode = bm;
}

void Canvas_MoveLayer(Canvas* canvas, int from, int to) {
    if (from < 0 || from >= canvas->layerCount || to < 0 || to >= canvas->layerCount) return;
    if (from == to) return;

    Image movedImage = canvas->layerImages[from];
    sLayerProps movedProps = canvas->layerProps[from];

    if (from < to) {
        for (int i = from; i < to; i++) {
            canvas->layerImages[i] = canvas->layerImages[i + 1];
            canvas->layerProps[i] = canvas->layerProps[i + 1];
        }
    } else {
        for (int i = from; i > to; i--) {
            canvas->layerImages[i] = canvas->layerImages[i - 1];
            canvas->layerProps[i] = canvas->layerProps[i - 1];
        }
    }

    canvas->layerImages[to] = movedImage;
    canvas->layerProps[to] = movedProps;
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

void Canvas_DuplicateLayer(Canvas* canvas, int idx) {
    if (idx < 0 || idx >= canvas->layerCount) return;
    Canvas_InsertLayer(canvas, idx + 1);
    canvas->layerImages[idx + 1] = ImageCopy(canvas->layerImages[idx]);
    canvas->layerProps[idx + 1] = canvas->layerProps[idx];
}

void Layer_ApplyTransform(sLayerProps* lp, const float mat[6]) {
    // mat and lp->mat are 2×3 row-major: [a, b, tx, c, d, ty]
    // Compose: new_mat = mat * lp->mat  (given * current)
    float a = mat[0], b = mat[1], tx = mat[2];
    float c = mat[3], d = mat[4], ty = mat[5];
    float ca = lp->mat[0], cb = lp->mat[1], ctx = lp->mat[2];
    float cc = lp->mat[3], cd = lp->mat[4], cty = lp->mat[5];
    lp->mat[0] = a * ca + b * cc;
    lp->mat[1] = a * cb + b * cd;
    lp->mat[2] = a * ctx + b * cty + tx;
    lp->mat[3] = c * ca + d * cc;
    lp->mat[4] = c * cb + d * cd;
    lp->mat[5] = c * ctx + d * cty + ty;
}

