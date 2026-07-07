#ifndef BRUSH_DRAW_H
#define BRUSH_DRAW_H

#include <stdint.h>
#include "raylib.h"
#include "texture_manager.h"

struct BPConfig {
    float userMax;
    float userMin;
    float outMin, outMax;
    float power;
    int   modulatorId;
    float jitter;
};

struct UserBrushConfig {
    int toolMode;

    BPConfig size;
    BPConfig hardness;
    BPConfig curvature;
    BPConfig opacity;
    BPConfig angle;
    BPConfig scaleRel;
    BPConfig cloneOpacity;
    BPConfig hue;
    BPConfig sat;
    BPConfig lit;
    BPConfig texScale;
    BPConfig texFeather;
    BPConfig texThresh;
    BPConfig texBlendVal;
    BPConfig power;
    BPConfig perspective;
    BPConfig focalOffset;
    BPConfig sizeMul;
    BPConfig spacing;

    int   texBlendMode;
    int   texNoisemode;
    int   texColorMode;
    bool  useTexLumAsAlpha;
    int   bmidx;
    uint8_t preserveop;
    uint8_t eraseMode;
    float userTexOriginX;
    float userTexOriginY;
    float userTexDirection;
    uint16_t baseSeed;
};

struct ModulatedBrushConfig {
    float radOut, radInRatio, scaleRel, resangle, opacity, crv, cop;
    Color col;
    float pwr, perspective, texScale, texFeather, texThresh, texBlendVal;
    float focalOffset, spacing;
    int   texBlendMode, texNoisemode, texColorMode;
    bool  useTexLumAsAlpha;
    int   bmidx;
    uint8_t preserveop, eraseMode;
    float userTexOriginX, userTexOriginY, userTexDirection;
    uint16_t baseSeed;
    float jitRadOut, jitRadIn, jitOpacity, jitCrv, jitX2y;
    float jitHue, jitSat, jitLit, jitCloneOp, jitFocal;
};

void CaptureBrushConfig(UserBrushConfig* cfg);

struct DabBrush {
    float rad_out_px;
    float radInRatio;
    float scale_x, scale_y;
    float resangle;
    float opacity;
    float crv;
    float cop;
    Color col;
    float pwr;
    int   bmidx;
    uint8_t eraseMode;
    uint8_t preserveop;
    float perspective;
    float texScale, texFeather, texThresh, texBlendVal;
    int   texBlendMode, texNoisemode, texColorMode;
    bool  useTexLumAsAlpha;
    float userTexOriginX, userTexOriginY;
    float userTexDirection;
    float focalOffset;
    float spacing;

    float jitRadOut;
    float jitRadIn;
    float jitOpacity;
    float jitCrv;
    float jitX2y;
    float jitHue, jitSat, jitLit;
    float jitCloneOp;
    float jitFocal;
    uint16_t baseSeed;
};

struct DabPoint {
    float x, y, srcX, srcY;
    float srcRad, srcAngle;
    DabBrush brush;
};

struct SegmentData {
    Vector2 pos1, pos2;
    Vector2 ctrl0, ctrl3;
    DabBrush brushFrom, brush;
    uint8_t tool, seamless, pixelPerfect, isStrokeStart;
    uint16_t seed;
    float ppBias;          // pixel-perfect parity bias (-1=unused, 0=even, 0.5=odd)
    float smudgeSrcX, smudgeSrcY;
    TexSlotID targetSlot;
    uint8_t userTexBucket;
    uint8_t userTexSlot;
    int dabOffset;
    float initAngle;
};

void DrawSegment(const SegmentData& dseg, RenderTexture2D rt, Texture2D brushTex, bool useTexture, bool seamless, int dabOffset, bool pixelPerfect = false);

void SegDrawer_SetSegmentStart(float startRad, Vector2 startPos, SegmentData* seg);
void SegDrawer_ComputeSegmentEnd(const SegmentData& seg, int dabOffset, float initialRad,
                                  Vector2* outLastPos, float* outLastRad);

struct SegResult {
    Vector2 lastDabPos;
    float lastRadOut;
    float overdraw;
};

DabBrush BlendBrushes(DabBrush from, DabBrush to, float k);

void JitterBrush(DabBrush& b, uint16_t baseSeed, int dabIdx);

int DrawLinear(const SegmentData& seg, int dabOffset, float initialRad,
               DabPoint* outPoints, int maxOut, SegResult* res);

#endif
