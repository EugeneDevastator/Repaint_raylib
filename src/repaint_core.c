#include "repaint.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// PackedFloat functions
float PackedFloat_GetVal(PackedFloat* pf) {
    return (float)(pf->IntVal + pf->FVal / 255.0f);
}

void PackedFloat_SetVal(PackedFloat* pf, double val) {
    pf->IntVal = (int16_t)floor(val);
    pf->FVal = (uint8_t)floor((val - floor(val)) * 255.0f);
}

// d_PointF functions
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

// d_Brush pack/unpack
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

// Math functions (ported from Brushes.cpp)
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

// Initialize random seed
void InitCore() {
    srand((unsigned int)time(NULL));
}

// Generate brush texture using raylib (replaces scanline methods)
void GenerateBrushTexture(d_Brush* brush, Texture2D* outTexture) {
    int size = (int)ceilf(brush->Realb.rad_out * 2.0f) + 2;
    if (size < 4) size = 4;

    Image brushImage = GenImageColor(size, size, (Color){0, 0, 0, 0});

    // Generate radial gradient brush
    int center = size / 2;
    float rad_out = brush->Realb.rad_out;
    float rad_in = brush->Realb.rad_in;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (float)(x - center);
            float dy = (float)(y - center);
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > rad_out) continue;

            float t = dist / rad_out;
            float innerT = rad_in / rad_out;

            // Shape calculation
            float alpha = 1.0f;
            if (t > innerT && rad_out > rad_in) {
                alpha = 1.0f - ((t - innerT) / (1.0f - innerT));
            }

            // Apply curvature
            float crv = brush->Realb.crv;
            if (crv < 0) {
                float fpos = 1.0f + powf(t - 1.0f, 3.0f);
                alpha *= (fpos - t) * fabsf(crv) + t;
            } else if (crv > 0) {
                alpha *= (t * t * t - t) * crv + t;
            }

            alpha = fmaxf(0.0f, fminf(1.0f, alpha));

            Color pixelColor = brush->Realb.col;
            pixelColor.a = (unsigned char)(alpha * 255.0f);
            ImageDrawPixel(&brushImage, x, y, pixelColor);
        }
    }

    *outTexture = LoadTextureFromImage(brushImage);
    UnloadImage(brushImage);
}

// Draw brush stamp at position
void DrawBrushStamp(Texture2D brushTex, Vector2 position, d_Brush* brush, Color color) {
    float scale = brush->Realb.scale;
    float rotation = brush->Realb.resangle;

    Rectangle srcRect = {0, 0, (float)brushTex.width, (float)brushTex.height};
    Rectangle dstRect = {
        position.x - brush->Realb.rad_out * scale,
        position.y - brush->Realb.rad_out * scale,
        brush->Realb.rad_out * 2.0f * scale,
        brush->Realb.rad_out * 2.0f * scale
    };

    Vector2 origin = {brush->Realb.rad_out * scale, brush->Realb.rad_out * scale};
    DrawTexturePro(brushTex, srcRect, dstRect, origin, rotation, color);
}

// Canvas functions
Canvas Canvas_Create(int width, int height, Color bgColor) {
    Canvas canvas;
    canvas.width = width;
    canvas.height = height;
    canvas.backgroundColor = bgColor;
    canvas.layerCount = 0;
    canvas.layers = NULL;
    canvas.layerProps = NULL;

    // Add initial layer
    Canvas_AddLayer(&canvas);

    return canvas;
}

void Canvas_Destroy(Canvas* canvas) {
    for (int i = 0; i < canvas->layerCount; i++) {
        UnloadTexture(canvas->layers[i]);
    }
    if (canvas->layers) free(canvas->layers);
    if (canvas->layerProps) free(canvas->layerProps);
    canvas->layerCount = 0;
}

void Canvas_AddLayer(Canvas* canvas) {
    canvas->layerCount++;
    canvas->layers = realloc(canvas->layers, canvas->layerCount * sizeof(Texture2D));
    canvas->layerProps = realloc(canvas->layerProps, canvas->layerCount * sizeof(sLayerProps));

    // Create new layer texture
    Image layerImage = GenImageColor(canvas->width, canvas->height, (Color){0, 0, 0, 0});
    canvas->layers[canvas->layerCount - 1] = LoadTextureFromImage(layerImage);
    UnloadImage(layerImage);

    // Set default properties
    canvas->layerProps[canvas->layerCount - 1].op = 1.0f;
    canvas->layerProps[canvas->layerCount - 1].visible = true;
    canvas->layerProps[canvas->layerCount - 1].blendmode = 0;
    canvas->layerProps[canvas->layerCount - 1].presop = 0;
    canvas->layerProps[canvas->layerCount - 1].locked = false;
    canvas->layerProps[canvas->layerCount - 1].layerName[0] = '\0';
}

void Canvas_DeleteLayer(Canvas* canvas, int index) {
    if (index < 0 || index >= canvas->layerCount || canvas->layerCount <= 1) return;

    UnloadTexture(canvas->layers[index]);

    // Shift remaining layers
    for (int i = index; i < canvas->layerCount - 1; i++) {
        canvas->layers[i] = canvas->layers[i + 1];
        canvas->layerProps[i] = canvas->layerProps[i + 1];
    }

    canvas->layerCount--;
    canvas->layers = realloc(canvas->layers, canvas->layerCount * sizeof(Texture2D));
    canvas->layerProps = realloc(canvas->layerProps, canvas->layerCount * sizeof(sLayerProps));
}

void Canvas_SetLayerOpacity(Canvas* canvas, int layer, float op) {
    if (layer >= 0 && layer < canvas->layerCount) {
        canvas->layerProps[layer].op = op;
    }
}

void Canvas_SetLayerBlendMode(Canvas* canvas, int layer, int bm) {
    if (layer >= 0 && layer < canvas->layerCount) {
        canvas->layerProps[layer].blendmode = bm;
    }
}

void Canvas_SetLayerVisible(Canvas* canvas, int layer, bool visible) {
    if (layer >= 0 && layer < canvas->layerCount) {
        canvas->layerProps[layer].visible = visible;
    }
}
