#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

void ResolveBrushParams(const UserBrushConfig& cfg, d_RealBrush* out, int toolMode,
                         float initAngle, const float modValues[csSTOP]);
void ResolveBrushParamsMax(const UserBrushConfig& cfg, d_RealBrush* out,
                           int toolMode, float initAngle);
DabBrush MakeDabBrush(const UserBrushConfig& cfg, const d_RealBrush& resolved);

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy);

#endif
