#ifndef BRUSH_BLEND_H
#define BRUSH_BLEND_H

#include "raylib.h"
#include "brush_draw.h"

void BrushBlend_Init(void);
void BrushBlend_Shutdown(void);
void BrushBlend_ApplyStamp(RenderTexture2D dstRT, const CollapsedBrush& brush,
    Texture2D brushTex, bool useTexture,
    float stampX, float stampY, float srcX, float srcY,
    bool seamless, bool pixelPerfect);

#endif
