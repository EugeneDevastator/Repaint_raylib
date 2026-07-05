#ifndef BRUSH_DRAW_H
#define BRUSH_DRAW_H

#include <stdint.h>
#include "raylib.h"
#include "texture_manager.h"

// ── Single slider/pot configuration ───────────────────────────────
struct BPConfig {
    float userMax;      // clipmaxF — slider position (0–1)
    float userMin;      // clipminF
    float outMin;       // output min
    float outMax;       // output max
    float power;        // non-linear curve (1.0 = linear)
    int   modulatorId;  // csNone=0, csPressure=1, csVel=2, etc.
    float jitter;       // per-dab jitter amount (0–1)
};

// ── User-facing brush configuration ───────────────────────────────
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
    BPConfig sizeMul;     // size multiplier slider
    BPConfig spacing;     // dab spacing slider

    // Non-modulated flags from the brush struct
    int   texBlendMode;
    int   texNoisemode;
    int   texColorMode;
    bool  useTexLumAsAlpha;
    int   bmidx;
    uint8_t preserveop;
    float userTexOriginX;
    float userTexOriginY;
    float userTexDirection;
    uint16_t baseSeed;
};

// ── Capture current BParam globals into a config ──
void CaptureBrushConfig(UserBrushConfig* cfg);

// ── Drawing-space brush (collapsed from UI parameters) ──────────────
struct DabBrush {
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
    float focalOffset;   // -1..1, oblique cone tip offset for radial gradient
    float spacing;       // 0–1 multiplier for dab spacing

    // Per-dab jitter ranges (drawing-space units, 0 = no jitter)
    float jitRadOut;
    float jitRadIn;
    float jitOpacity;
    float jitCrv;
    float jitX2y;
    float jitHue, jitSat, jitLit;
    float jitCloneOp;
    float jitFocal;      // per-dab focal offset jitter
    uint16_t baseSeed;
};

// ── Point output from DrawLinear (replaces C callback) ─────────────
struct DabPoint {
    float x, y, srcX, srcY;
    float srcRad, srcAngle;   // source dab radius + rotation (degrees) for smudge transform
    DabBrush brush;
};

// ── Unified segment struct ─────────────────────────────────────────
struct SegmentData {
    Vector2 pos1, pos2;
    Vector2 ctrl0, ctrl3;
    DabBrush brushFrom, brush;
    uint8_t tool, seamless, pixelPerfect, isStrokeStart;
    uint16_t seed;
    float smudgeSrcX, smudgeSrcY;
    TexSlotID targetSlot;
    uint8_t userTexBucket;   // TM_BUCKET_USER typically, 0xFF = none
    uint8_t userTexSlot;     // slot within bucket, 0xFF = none
    int dabOffset;
    float initAngle;    // base angle for per-dab rotation modulation; 0 = use baked resangle
};

// ── Stateless: draws one segment onto a render target ──────────────
void DrawSegment(const SegmentData& dseg, RenderTexture2D rt, Texture2D brushTex, bool useTexture, bool seamless, int dabOffset, bool pixelPerfect = false);

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
DabBrush BlendBrushes(DabBrush from, DabBrush to, float k);

// ── Apply per-dab jitter from ranges (uses deterministic rng) ──────
void JitterBrush(DabBrush& b, uint16_t baseSeed, int dabIdx);

// ── Linear stroke: places next dab only when distance >= spacing ──
int DrawLinear(const SegmentData& seg, int dabOffset, float initialRad,
               DabPoint* outPoints, int maxOut, SegResult* res);

#endif
