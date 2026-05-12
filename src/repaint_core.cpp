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

void d_PointF_SetByVector2(d_PointF* p, Vector2 src) {
    PackedFloat_SetVal(&p->xpos, src.x);
    PackedFloat_SetVal(&p->ypos, src.y);
}

Vector2 d_PointF_ToVector2(d_PointF* p) {
    Vector2 res;
    res.x = PackedFloat_GetVal(&p->xpos);
    res.y = PackedFloat_GetVal(&p->ypos);
    return res;
}

void d_Brush_SelfPack(d_Brush* brush) {
    PackedFloat_SetVal(&brush->Pack.Prad_in, brush->Realb.rad_in);
    PackedFloat_SetVal(&brush->Pack.Prad_out, brush->Realb.rad_out);
    brush->Pack.bmidx = brush->Realb.bmidx;
    brush->Pack.col = brush->Realb.col;
    brush->Pack.cop = (uint8_t)(brush->Realb.cop * 255.0f);
    brush->Pack.crv = (uint8_t)(brush->Realb.crv * 255.0f);
    brush->Pack.MaskID = brush->Realb.MaskID;
    brush->Pack.NoiseID = brush->Realb.NoiseID;
    brush->Pack.noisex = brush->Realb.noisex;
    brush->Pack.noisey = brush->Realb.noisey;
    brush->Pack.noiseidx = brush->Realb.noiseidx;
    brush->Pack.seed = brush->Realb.seed;
    brush->Pack.pipeID = brush->Realb.pipeID;
    brush->Pack.resangle = (uint16_t)(brush->Realb.resangle * 65535.0f / 360.0f);
    brush->Pack.scale = (uint8_t)(brush->Realb.scale * 51.0f);
    brush->Pack.sol = (uint8_t)(brush->Realb.sol * 255.0f);
    brush->Pack.sol2op = (uint8_t)(brush->Realb.sol2op * 255.0f);
    brush->Pack.x2y = (uint8_t)(brush->Realb.x2y * 255.0f);
    brush->Pack.preserveop = brush->Realb.preserveop;
    brush->Pack.pwr = (uint8_t)((brush->Realb.pwr + 1.0f) * 127.0f);
}

void d_Brush_SelfUnpack(d_Brush* brush) {
    brush->Realb.rad_in = PackedFloat_GetVal(&brush->Pack.Prad_in);
    brush->Realb.rad_out = PackedFloat_GetVal(&brush->Pack.Prad_out);
    brush->Realb.bmidx = brush->Pack.bmidx;
    brush->Realb.col = brush->Pack.col;
    brush->Realb.opacity = (float)brush->Pack.col.a / 255.0f;
    brush->Realb.cop = brush->Pack.cop / 255.0f;
    brush->Realb.crv = brush->Pack.crv / 255.0f;
    brush->Realb.MaskID = brush->Pack.MaskID;
    brush->Realb.NoiseID = brush->Pack.NoiseID;
    brush->Realb.noisex = brush->Pack.noisex;
    brush->Realb.noisey = brush->Pack.noisey;
    brush->Realb.noiseidx = brush->Pack.noiseidx;
    brush->Realb.seed = brush->Pack.seed;
    brush->Realb.pipeID = brush->Pack.pipeID;
    brush->Realb.resangle = (float)brush->Pack.resangle * 360.0f / 65535.0f;
    brush->Realb.scale = (float)brush->Pack.scale * 5.0f / 255.0f;
    brush->Realb.sol = brush->Pack.sol / 255.0f;
    brush->Realb.sol2op = brush->Pack.sol2op / 255.0f;
    brush->Realb.x2y = brush->Pack.x2y / 255.0f;
    brush->Realb.preserveop = brush->Pack.preserveop;
    brush->Realb.pwr = (float)brush->Pack.pwr / 127.0f - 1.0f;
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
    canvas->layerProps[idx].op = 1.0f;
    canvas->layerProps[idx].visible = true;
    canvas->layerProps[idx].blendmode = bmNormal;
    canvas->layerProps[idx].presop = 0;
    canvas->layerProps[idx].locked = false;
    canvas->layerProps[idx].layerName[0] = '\0';
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

void Canvas_DuplicateLayer(Canvas* canvas, int idx) {
    if (idx < 0 || idx >= canvas->layerCount) return;
    Canvas_InsertLayer(canvas, idx + 1);
    canvas->layerImages[idx + 1] = ImageCopy(canvas->layerImages[idx]);
    canvas->layerProps[idx + 1] = canvas->layerProps[idx];
}
// must be used via shader - fix later.
static void ApplyBlendMode(Color* dst, Color srcCol, float srcA, float dstA, int mode) {
    float sr = srcCol.r / 255.0f, sg = srcCol.g / 255.0f, sb = srcCol.b / 255.0f;
    float dr = dst->r / 255.0f, dg = dst->g / 255.0f, db = dst->b / 255.0f;
    float sa = fminf(srcA, 1.0f);
    float da = fminf(dstA, 1.0f);
    float outr, outg, outb, outa;

    switch (mode) {
    case 1: // Plus
        outr = dr + sr * sa; outg = dg + sg * sa; outb = db + sb * sa;
        outa = fminf(1.0f, da + sa);
        break;
    case 2: // Dodge
        outr = dr + sr * sa * (1.0f - dr); outg = dg + sg * sa * (1.0f - dg); outb = db + sb * sa * (1.0f - db);
        outa = fminf(1.0f, da + sa);
        break;
    case 3: // Screen
        outr = 1.0f - (1.0f - dr) * (1.0f - sr * sa); outg = 1.0f - (1.0f - dg) * (1.0f - sg * sa); outb = 1.0f - (1.0f - db) * (1.0f - sb * sa);
        outa = 1.0f - (1.0f - da) * (1.0f - sa);
        break;
    case 4: // Lighten
        outr = fmaxf(dr, sr * sa); outg = fmaxf(dg, sg * sa); outb = fmaxf(db, sb * sa);
        outa = fmaxf(da, sa);
        break;
    case 5: // Burn
        outr = 1.0f - (1.0f - dr) / (sr * sa + 0.001f); outg = 1.0f - (1.0f - dg) / (sg * sa + 0.001f); outb = 1.0f - (1.0f - db) / (sb * sa + 0.001f);
        outa = fminf(1.0f, da + sa);
        break;
    case 6: // Multiply
        outr = dr * (1.0f - sa + sr * sa); outg = dg * (1.0f - sa + sg * sa); outb = db * (1.0f - sa + sb * sa);
        outa = da;
        break;
    case 7: // Darken
        outr = fminf(dr, sr * sa); outg = fminf(dg, sg * sa); outb = fminf(db, sb * sa);
        outa = fminf(da, sa);
        break;
    case 8: // Normal (straight)
    default:
        outr = sr * sa + dr * (1.0f - sa); outg = sg * sa + dg * (1.0f - sa); outb = sb * sa + db * (1.0f - sa);
        outa = sa + da * (1.0f - sa);
        break;
    }

    dst->r = (uint8_t)(fminf(fmaxf(outr, 0.0f), 1.0f) * 255.0f);
    dst->g = (uint8_t)(fminf(fmaxf(outg, 0.0f), 1.0f) * 255.0f);
    dst->b = (uint8_t)(fminf(fmaxf(outb, 0.0f), 1.0f) * 255.0f);
    dst->a = (uint8_t)(fminf(fmaxf(outa, 0.0f), 1.0f) * 255.0f);
}

void Canvas_MergeDown(Canvas* canvas, int idx) {
    if (idx <= 0 || idx >= canvas->layerCount) return;
    float layerOp = canvas->layerProps[idx].op;
    int blendMode = canvas->layerProps[idx].blendmode;
    Color* src = (Color*)canvas->layerImages[idx].data;
    Color* dst = (Color*)canvas->layerImages[idx - 1].data;
    int n = canvas->width * canvas->height;
    for (int i = 0; i < n; i++) {
        float sa = src[i].a / 255.0f * layerOp;
        float da = dst[i].a / 255.0f;
        if (sa < 0.001f) continue;
        ApplyBlendMode(&dst[i], src[i], sa, da, blendMode);
    }
    Canvas_DeleteLayer(canvas, idx);
}
