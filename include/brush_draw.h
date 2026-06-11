#ifndef BRUSH_DRAW_H
#define BRUSH_DRAW_H

#include <stdint.h>
#include "raylib.h"

// ── Drawing-space brush (collapsed from UI parameters) ──────────────
struct CollapsedBrush {
    float rad_out_px;   // outer radius in pixels
    float radInRatio;   // 0–1
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
    float spacing;       // 0–1 multiplier for dab spacing

    // Per-dab jitter ranges (drawing-space units, 0 = no jitter)
    float jitRadOut;
    float jitRadIn;
    float jitOpacity;
    float jitCrv;
    float jitX2y;
    float jitHue, jitSat, jitLit;
    float jitCloneOp;
    uint16_t baseSeed;
};

// ── Per-dab payload (resolved at draw time by DabDrawer) ───────────
struct DabData {
    float x, y, srcX, srcY;
    CollapsedBrush brush;
    uint8_t targetType;   // 0=layer, 1=brushTex
    uint8_t targetId;     // layer index or brush texture index
    uint8_t userTexIdx;   // 0 = no texture, 1+ = (index+1) into state->brushTex[]
    bool seamless;
    bool pixelPerfect;
};

// ── Unified segment struct (replaces DrawSegment + NetSegment) ─────
struct SegmentData {
    Vector2 pos1, pos2;
    Vector2 ctrl0, ctrl3;
    CollapsedBrush brushFrom, brush;
    uint8_t tool, seamless, pixelPerfect;
    uint16_t seed;
    float smudgeSrcX, smudgeSrcY;
    uint8_t targetType, targetId;
    uint8_t userTexIdx;   // 0 = no texture, 1+ = (index+1) into state->brushTex[]
    int dabOffset;
    float initAngle;    // base angle for per-dab rotation modulation; 0 = use baked resangle
};

// ── Stateless: draws one segment onto a render target ──────────────
void DrawOneSegment(const SegmentData& dseg, RenderTexture2D rt, Texture2D brushTex, bool useTexture, bool seamless, int dabOffset, bool pixelPerfect = false);

// ── Segment computation helpers (no rendering) ─────────────────────
void SegDrawer_SetSegmentStart(float startRad, Vector2 startPos, SegmentData* seg);
void SegDrawer_ComputeSegmentEnd(const SegmentData& seg, int dabOffset, float initialRad,
                                  Vector2* outLastPos, float* outLastRad);

// ── Segment result ─────────────────────────────────────────────────
struct SegResult {
    Vector2 lastDabPos;
    float lastRadOut;
    float overdraw;
};

// ── Blend brushes (interpolate all fields) ─────────────────────────
CollapsedBrush BlendBrushes(CollapsedBrush from, CollapsedBrush to, float k);

// ── Apply per-dab jitter from ranges (uses deterministic rng) ──────
void JitterBrush(CollapsedBrush& b, uint16_t baseSeed, int dabIdx);

// ── Linear stroke: places next dab only when distance >= spacing ──
int DrawLinear(const SegmentData& seg, int dabOffset, float initialRad,
               void (*apply)(float x, float y, float srcX, float srcY, const CollapsedBrush& brush, void* user),
               void* user, int maxOut, SegResult* res);

#endif
