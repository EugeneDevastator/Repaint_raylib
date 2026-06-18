#include "repaint.h"
#include "layerstack.h"

// After a brush stroke, sync the layer RT to its CPU image.
void SyncLayerTexture(AppState* state, int layer) {
    LayerStack_SyncImageFromRT(layer);
}
