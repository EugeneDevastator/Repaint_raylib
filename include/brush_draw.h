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

// ── Segment for the drawer ─────────────────────────────────────────
struct DrawSegment {
    Vector2 pos1, pos2;
    CollapsedBrush brushFrom, brush;
    float spacing;       // 0–1 multiplier
    uint8_t Noisemode;
    uint16_t seed;
};

// ── Output dab ─────────────────────────────────────────────────────
struct DrawDab {
    float x, y;
    float srcX, srcY;
    CollapsedBrush brush;
};

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
int DrawLinear(const DrawSegment* seg, int dabOffset, float initialRad, DrawDab* out, int maxOut, SegResult* res);

// ── Airflow stroke: accumulates overdraw, bursty placement ─────────
int DrawAirflow(const DrawSegment* seg, float accum, DrawDab* out, int maxOut, SegResult* res);

#endif
