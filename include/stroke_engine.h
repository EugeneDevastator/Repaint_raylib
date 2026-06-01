#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

const int STROKE_SPLINE_POINTS = 256;

// ── Bridge: collapse UI brush state into drawing-space brush ──
// Called by StrokeEngine before passing to segment drawer.
// Converts size × SizeMul → rad_out_px, x2y → scale_x/scale_y,
// hardness → radInRatio, and fills jitter ranges from BParams.
CollapsedBrush CollapseBrushParams(const d_RealBrush& uiBrush, float initialAngle, int toolMode);

// ── StrokeEngine ──
void StrokeEngine_Init(StrokeEngine* se);
void StrokeEngine_BeginStroke(StrokeEngine* se, const d_Brush* baseBrush, float x, float y);
int  StrokeEngine_FeedPoint(StrokeEngine* se, const StrokePoint& sp,
                            const d_RealBrush* baseBrush,
                            float initialAngle, int toolMode);
void StrokeEngine_EndStroke(StrokeEngine* se);
int  StrokeEngine_FlushSmoothing(StrokeEngine* se, const d_RealBrush* baseBrush,
                                  float initialAngle, int toolMode);

// ── Utilities ──
void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, int toolMode,
                              float cx, float cy);

#endif