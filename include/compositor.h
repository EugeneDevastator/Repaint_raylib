#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "repaint.h"

typedef struct {
    float opacity;
    int blendMode;
    float threshold;
    float feather;
    bool seamless;
} CompositorBlendParams;

void Compositor_Init(void);
void Compositor_Shutdown(void);
void Compositor_ReloadShader(void);

// Blit a texture onto dst, clipped to dstRegion (scissor in dst pixels).
// combined = viewXform->mat * xform->mat  (compositor multiplies them internally)
// For params->seamless: 3×3 tile wrap using viewXform as reference frame.
void Compositor_BlitLayerOnto(
    Texture2D srcTex, const RectXform* xform,
    const CompositorBlendParams* params,
    const RectXform* viewXform,
    RenderTexture2D dst, Rectangle dstRegion);

// Merge top texture into bottom RT. Pure wrapper around BlitLayerOnto.
void Compositor_ApplyLayerToLayer(
    Texture2D topTex, const RectXform* topXform,
    const CompositorBlendParams* params,
    RenderTexture2D bottomRT, const RectXform* bottomXform);

// Present shader access
bool     Compositor_PresentInited(void);
Shader   Compositor_GetPresentShader(void);
void     Compositor_SetPresentTexSize(int w, int h);
void     Compositor_SetPresentDither(bool on);

// Checker texture
void     Compositor_EnsureChecker(int w, int h);
Texture2D Compositor_GetCheckerTex(void);

#endif
