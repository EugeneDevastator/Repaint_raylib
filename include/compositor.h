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

// Merge top texture into bottom RT. Thin wrapper around QuadApply.
void Compositor_ApplyLayerToLayer(
    Texture2D topTex, const RectXform* topXform,
    const CompositorBlendParams* params,
    RenderTexture2D bottomRT, const RectXform* bottomXform);

// Quad apply — the universal compositing operation.
// Applies src Quad onto dst Quad using src->xform as world-transform
// and inverse(dst->xform) as the view matrix. Everything else is
// derived automatically (pixel size, blending).
void Compositor_QuadApply(const Quad* src, const CompositorBlendParams* bp, const Quad* dst);

// Present shader access
bool     Compositor_PresentInited(void);
Shader   Compositor_GetPresentShader(void);
void     Compositor_SetPresentTexSize(int w, int h);
void     Compositor_SetPresentDither(bool on);
void     Compositor_SetPresentNearest(bool on);

// Checker texture
void     Compositor_EnsureChecker(int w, int h);
Texture2D Compositor_GetCheckerTex(void);

#endif
