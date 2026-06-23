#include "repaint.h"
#include "compositor.h"
#include "layerstack.h"
#include "rlgl.h"

RenderTexture2D* DocBlender_Composite(AppState* state) { (void)state; return Compositor_Composite(); }
void ReloadViewportShader(void) { Compositor_ReloadShader(); }
void MergeDownLayer(AppState* state, int idx) { (void)state; LayerStack_MergeDown(idx); }
bool GetPresentInited(void) { return Compositor_PresentInited(); }
Shader GetPresentShader(void) { return Compositor_GetPresentShader(); }
Image CompositeLayersWithDither(AppState* state) { (void)state; return Compositor_CompositeWithDither(); }
void UnloadViewportRenderer(void) { LayerStack_Shutdown(); Compositor_Shutdown(); }
