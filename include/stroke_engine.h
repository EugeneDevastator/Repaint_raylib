#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"

// Configuration constants
const float STROKE_ANGLE_BREAK_THRESHOLD = 30.0f;
const int   STROKE_MAX_SPLINE_POINTS = 4;
const int   STROKE_SPLINE_SUBDIVS = 8;

// ── SegmentResult ──
struct SegmentResult {
    Vector2 lastDabPos;
    float overdraw;
};

// ── LinearStroke: places dabs at exact spacing from lastDabPos ──
// No overdraw accumulation. Only places a dab when distance from
// section->Stroke.pos1 to pos2 reaches spacing. This gives perfectly
// even dab spacing regardless of input timing.
int SegmentDrawer_DrawLinear(
    const d_Section* section,
    BrushDab* outDabs,
    int maxDabs,
    SegmentResult* outResult);

// ── AirflowStroke: accumulates overdraw for bursty placement ──
// Preserves the old dabAccum behavior: leftover distance carries over
// between frames, eventually releasing a burst of dabs.
int SegmentDrawer_DrawAirflow(
    const d_Section* section,
    float initialDabAccum,
    BrushDab* outDabs,
    int maxDabs,
    SegmentResult* outResult);

// ── StrokeEngine ──
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
