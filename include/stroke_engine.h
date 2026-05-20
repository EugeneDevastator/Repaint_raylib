#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"

// ── SegmentResult: output from drawing a single segment ──
struct SegmentResult {
    Vector2 lastDabPos;
    float overdraw;  // leftover spacing for next segment
};

// ── SegmentDrawer: interpolates dabs along a pre-computed segment ──
// Walks from section->Stroke.pos1 to pos2, places dabs at spacing intervals.
// Interpolates brush between BrushFrom and Brush.
// Applies deterministic scatter based on seed.
// No parameter jitter — that's the caller's concern.
// initialDabAccum: leftover spacing from previous segment for chaining.
// outResult: returns last dab position and overdraw for chaining.
// Returns number of dabs placed in outDabs (max maxDabs).
int SegmentDrawer_Draw(
    const d_Section* section,
    float initialDabAccum,
    BrushDab* outDabs,
    int maxDabs,
    SegmentResult* outResult);

// ── StrokeEngine: builds segments from real-time input, adds jitter ──
// Uses modulators + geometry to form segment params, then calls SegmentDrawer.

void StrokeEngine_Init(StrokeEngine* se);
void StrokeEngine_BeginStroke(StrokeEngine* se, const d_Brush* baseBrush, float x, float y);
int  StrokeEngine_FeedPoint(StrokeEngine* se, const StrokePoint& sp,
                            const d_RealBrush* baseBrush,
                            float initialAngle, int toolMode,
                            BrushDab* outDabs, int maxDabs);
void StrokeEngine_EndStroke(StrokeEngine* se);

// ── Utilities ──
void StrokeEngine_ApplyDabs(RenderTexture2D dstRT, Texture2D brushTex,
                            BrushDab* dabs, int n);
void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, float cx, float cy);

#endif
