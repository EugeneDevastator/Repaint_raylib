#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "repaint.h"

void LayerStack_Init(void);
void LayerStack_Shutdown(void);
void LayerStack_ReloadShader(void);
void LayerStack_Bind(AppState* state);

int  LayerStack_InsertLayer(int afterIdx);
void LayerStack_DeleteLayer(int idx);
void LayerStack_DuplicateLayer(int idx);
void LayerStack_MoveLayer(int from, int to);
void LayerStack_MergeDown(int idx);
void LayerStack_MergeDownSeamless(int idx);

RenderTexture2D* LayerStack_Composite(void);
Image LayerStack_CompositeWithDither(void);
bool  LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);

#endif
