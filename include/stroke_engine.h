#ifndef STROKE_ENGINE_H
#define STROKE_ENGINE_H

#include "repaint.h"
#include "brush_draw.h"

ModulatedBrushConfig ResolveModulatedConfig(const UserBrushConfig& cfg, int toolMode,
                                             float initAngle, const ModulatorTable* mods);
ModulatedBrushConfig ResolveModulatedConfigMax(const UserBrushConfig& cfg, int toolMode,
                                               float initAngle);
DabBrush MakeDabBrush(const ModulatedBrushConfig& mod, const float rad_out_px_override = 0.0f);

// Generate dab points only (no rendering) — caller renders incrementally
int StrokeEngine_GeneratePreviewDabs(const d_RealBrush* baseBrush, int toolMode,
                                     float initialAngle, float cx, float cy,
                                     DabPoint* outBuf, int maxOut);

#endif
