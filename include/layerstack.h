#ifndef LAYERSTACK_H
#define LAYERSTACK_H

// ── LayerStack module ───────────────────────────────────────────────
// Owns all layer data (images, props, GPU render-targets and display
// textures), compositing, merge operations, and the layer-blend shader.
// Binds to an AppState once at init via LayerStack_Bind(); after that
// all layer operations go through this module.

#include "repaint.h"

void LayerStack_Init(void);
void LayerStack_Shutdown(void);
void LayerStack_ReloadShader(void);
void LayerStack_Bind(AppState* state);

int  LayerStack_AddNew(int w, int h);
int  LayerStack_InsertLayer(int afterIdx);
void LayerStack_DeleteLayer(int idx);
void LayerStack_DuplicateLayer(int idx);
void LayerStack_MoveLayer(int from, int to);
void LayerStack_ApplyTransform(int idx, const float mat[6]);
void LayerStack_MergeDown(int idx);
void LayerStack_MergeDownSeamless(int idx);

RenderTexture2D* LayerStack_Composite(void);
Image LayerStack_CompositeWithDither(void);
bool  LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);

#endif
