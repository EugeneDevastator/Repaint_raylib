#include "repaint.h"
#include "layerstack.h"
#include "rlgl.h"

bool layersDirty = true;

// ── These functions now delegate to the layerstack module ────────────

RenderTexture2D* DocBlender_Composite(AppState* state) {
    (void)state;
    return LayerStack_Composite();
}

void ReloadViewportShader(void) {
    LayerStack_ReloadShader();
}

void MergeDownLayer(AppState* state, int idx) {
    (void)state;
    LayerStack_MergeDown(idx);
}

bool GetPresentInited(void) {
    return LayerStack_PresentInited();
}

Shader GetPresentShader(void) {
    return LayerStack_GetPresentShader();
}

Image CompositeLayersWithDither(AppState* state) {
    (void)state;
    return LayerStack_CompositeWithDither();
}

void UnloadViewportRenderer(void) {
    LayerStack_Shutdown();
}
