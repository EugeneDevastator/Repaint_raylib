#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// ── LayerStack — self-contained layer data source ───────────────────
// Owns all layer images, props, GPU render targets, display textures,
// and the layer-blend shader.
// No AppState dependency — everything is internal.

#include "repaint.h"

void LayerStack_Init(void);
void LayerStack_Shutdown(void);
void LayerStack_ReloadShader(void);

// Canvas RT — the output texture that is composited onto the screen.
// Its pixel size is the canvas resolution (independent of world-region ww/wh).
// Created via LayerStack_InitCanvas, resized via LayerStack_ResizeCanvas.
void LayerStack_InitCanvas(int w, int h);
void LayerStack_SetCanvasXform(const RectXform* xf);
void LayerStack_ResizeCanvas(int newW, int newH);
// Canvas Quad — xform + RT as a first-class object.
// Use LayerStack_GetCanvasQuadPtr() to get a pointer to live storage.
Quad* LayerStack_GetCanvasQuadPtr(void);
Quad* LayerStack_GetQuadPtr(int idx);
RenderTexture2D LayerStack_GetCanvasRT(void);
RenderTexture2D* LayerStack_GetCanvasRTPtr(void);
int             LayerStack_RenderW(void);
int             LayerStack_RenderH(void);

// Canvas view matrix — maps world → canvas-local space.
// The compositor uses this to blit layers onto the canvas RT.
void LayerStack_SetCanvasView(const float mat[6]);
const float* LayerStack_GetCanvasView(void);

// ── Layer management ────────────────────────────────────────────────
int  LayerStack_Add(int w, int h);
int  LayerStack_InsertLayer(int afterIdx);
void LayerStack_DeleteLayer(int idx);
void LayerStack_DuplicateLayer(int idx);
void LayerStack_DuplicateAsInstance(int idx);
void LayerStack_MoveLayer(int from, int to);

// ── Accessors ───────────────────────────────────────────────────────
int            LayerStack_Count(void);
sLayerProps*   LayerStack_GetProps(int idx);
RenderTexture2D LayerStack_GetRT(int idx);
TexSlotID      LayerStack_GetSlotID(int idx);
int            LayerStack_RenderW(void);
int            LayerStack_RenderH(void);

// ── Reverse lookup ──────────────────────────────────────────────────
int  LayerStack_FindLayerBySlot(TexSlotID slot);

// ── GPU ↔ CPU transfer (caller owns + frees the Image) ──────────────
Image LayerStack_ReadFromGPU(int idx);
void  LayerStack_UploadToGPU(int idx, Image img);

// Scene bounds — computes the bounding rectangle (in document-space units)
// of all visible layers. Returns false if no visible layers.
bool LayerStack_GetSceneBounds(Rectangle* out);

// Bake a single layer's transformed content into a caller-owned RT
void LayerStack_BakeSingleLayer(int idx, RenderTexture2D dst);

// Canvas-window commit: pre-multiply canvasView into every layer's
// transform (non-destructive — image data is untouched).
void LayerStack_BakeCanvasWindow(const Document* doc);

// For viewport/renderer access
const float* LayerStack_GetCanvasView(void);

// Resize a layer's texture (image content is scaled into the new RT).
void LayerStack_ResizeLayer(int idx, int newW, int newH);

#endif
