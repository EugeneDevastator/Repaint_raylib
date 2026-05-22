#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "repaint.h"

// ── Init / shutdown ────────────────────────────────────────────────
void LayerStack_Init(int canvasW, int canvasH);
void LayerStack_Shutdown(void);
void LayerStack_ReloadShader(void);
void LayerStack_Bind(AppState* state);  // connect to AppState layer data

// ── Query ──────────────────────────────────────────────────────────
int  LayerStack_Count(void);
int  LayerStack_Width(void);
int  LayerStack_Height(void);
bool LayerStack_Dirty(void);

// ── Layer data access ──────────────────────────────────────────────
sLayerProps*    LayerStack_GetProps(int idx);
Image*          LayerStack_GetImage(int idx);
RenderTexture2D LayerStack_GetRT(int idx);
Texture2D       LayerStack_GetTex(int idx);   // display texture

// ── Management ─────────────────────────────────────────────────────
int  LayerStack_Add(void);         // returns new layer index
void LayerStack_Delete(int idx);
void LayerStack_Duplicate(int idx);
void LayerStack_Move(int from, int after);
void LayerStack_MergeDown(int idx);
void LayerStack_ApplyTransform(int idx, const float mat[6]);

// ── Sync between GPU RT ↔ CPU image ───────────────────────────────
void LayerStack_SyncRTFromImage(int idx);
void LayerStack_SyncImageFromRT(int idx);
void LayerStack_SyncAllRTs(void);
void LayerStack_SyncAllImages(void);
void LayerStack_SyncLayerTex(int idx);  // RT → display texture

// ── Compositing (viewport display) ─────────────────────────────────
// Returns a pointer to the composited 16-bit render texture.
// Re-composites only when dirty.
RenderTexture2D* LayerStack_Composite(void);

// ── Export helpers ─────────────────────────────────────────────────
Image LayerStack_CompositeWithDither(void);
bool  LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);

#endif
