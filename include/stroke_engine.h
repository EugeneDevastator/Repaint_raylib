#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

// ── Apply modulation (bpAngle, bpSize, …) to a brush via current g_modPars ──
d_RealBrush ModulateBrushParams(const d_RealBrush& brush, float initAngle, int toolMode);

// ── Bridge: collapse UI brush state into drawing-space brush ──
CollapsedBrush CollapseBrushParams(const d_RealBrush& uiBrush, float initialAngle, int toolMode);

// ── Utilities ──
// Draws a preview stamp + path simulating what emitSegment produces.
// initialAngle is the user-set base angle (state->initialAngle).
void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy);

#endif
