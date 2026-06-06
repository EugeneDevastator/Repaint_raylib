#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// ── LayerStack — self-contained layer data source ───────────────────
// Owns all layer images, props, GPU render targets, display textures,
// compositing, and the layer-blend shader.
// No AppState dependency — everything is internal.

#include "repaint.h"

void LayerStack_Init(void);
void LayerStack_Shutdown(void);
void LayerStack_ReloadShader(void);

// Set the rendering window (for Composite output) and init accumulators
void LayerStack_SetRenderWindow(int w, int h);

// ── Layer management ────────────────────────────────────────────────
int  LayerStack_Add(int w, int h);
int  LayerStack_InsertLayer(int afterIdx);
void LayerStack_DeleteLayer(int idx);
void LayerStack_DuplicateLayer(int idx);
void LayerStack_DuplicateAsInstance(int idx);
void LayerStack_MoveLayer(int from, int to);
void LayerStack_ApplyTransform(int idx, const float mat[6]);
void LayerStack_MergeDown(int idx);
void LayerStack_MergeDownSeamless(int idx);

// ── Accessors ───────────────────────────────────────────────────────
int            LayerStack_Count(void);
sLayerProps*   LayerStack_GetProps(int idx);
Image*         LayerStack_GetImage(int idx);
RenderTexture2D LayerStack_GetRT(int idx);
Texture2D       LayerStack_GetTex(int idx);
int            LayerStack_RenderW(void);
int            LayerStack_RenderH(void);

// ── Sync (GPU ↔ CPU) ────────────────────────────────────────────────
void LayerStack_SyncImageFromRT(int idx);
void LayerStack_SyncLayerTex(int idx);

// Upload the CPU image to the GPU render target (CPU → GPU)
void LayerStack_SyncRTFromImage(int idx);

// ── Compositing ─────────────────────────────────────────────────────
// Legacy — renders into internal accumulators at SetRenderWindow resolution
RenderTexture2D* LayerStack_Composite(void);
Image LayerStack_CompositeWithDither(void);

// New — render at arbitrary resolution with caller-owned destination
// viewMat: 2×3 affine (NULL = identity). dst is written in-place.
void LayerStack_ProduceCompositeView(RenderTexture2D dst, const float viewMat[6], int w, int h);
void LayerStack_ProduceCompositeDitherView8b(Image* dst, const float viewMat[6], int w, int h);
void LayerStack_ProduceComposite(RenderTexture2D dst, int w, int h);
void LayerStack_ProduceCompositeDither8b(Image* dst, int w, int h);

// ── For viewport/renderer access ─────────────────────────────────────
bool   LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);
void   LayerStack_SetPresentTexSize(int w, int h);
void   LayerStack_SetPresentDither(bool on);
Texture2D LayerStack_GetCheckerTex(void);
void   LayerStack_SetDirty(void);

// ── Viewport resolution toggle ──
extern bool g_useViewRes;

#endif
