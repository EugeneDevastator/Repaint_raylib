#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

ModulatedBrushConfig ResolveModulatedConfig(const UserBrushConfig& cfg, int toolMode,
                                             float initAngle, const float modValues[csSTOP]);
ModulatedBrushConfig ResolveModulatedConfigMax(const UserBrushConfig& cfg, int toolMode,
                                               float initAngle);
DabBrush MakeDabBrush(const ModulatedBrushConfig& mod, const float rad_out_px_override = 0.0f);

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy);

#endif
