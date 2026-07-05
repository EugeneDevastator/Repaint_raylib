#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

void SetupBrushContext(const d_RealBrush& brush, int toolMode, float initAngle);

// ── Single resolution point: BParam sliders + modulator state → CollapsedBrush ──
CollapsedBrush GetCollapsedBrush(const float modValues[csSTOP]);
CollapsedBrush GetCollapsedBrushMax();

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy);

#endif
