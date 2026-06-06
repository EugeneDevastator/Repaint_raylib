#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

// ── Bridge: collapse UI brush state into drawing-space brush ──
CollapsedBrush CollapseBrushParams(const d_RealBrush& uiBrush, float initialAngle, int toolMode);

// ── Utilities ──
void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, int toolMode,
                              float cx, float cy);

#endif
