#include "repaint.h"
#include "layerstack.h"

// After a brush stroke, sync the layer RT to its CPU image and display texture.
void SyncLayerTexture(AppState* state, int layer) {
    LayerStack_SyncLayerTex(layer);
}
