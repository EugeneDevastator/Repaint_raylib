#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "repaint.h"

void Compositor_Init(void);
void Compositor_Shutdown(void);
void Compositor_ReloadShader(void);

// Canvas-resolution composite (cached via dirty flag)
RenderTexture2D* Compositor_Composite(void);

// Composite with dithering, returns 8-bit image
Image Compositor_CompositeWithDither(void);

// Composite into caller-owned RT at arbitrary resolution with view matrix
void Compositor_CompositeViewInto(RenderTexture2D dst, const float viewMat[6], int w, int h);

// Merge-down: blend top layer into bottom, returns merged RT (caller owns)
RenderTexture2D Compositor_MergeBlend(int topIdx, int bottomIdx, bool seamless);

// Present shader access
bool     Compositor_PresentInited(void);
Shader   Compositor_GetPresentShader(void);
void     Compositor_SetPresentTexSize(int w, int h);
void     Compositor_SetPresentDither(bool on);

// Checker texture
Texture2D Compositor_GetCheckerTex(void);

// Mark composite cache as dirty
void Compositor_SetDirty(void);

#endif
