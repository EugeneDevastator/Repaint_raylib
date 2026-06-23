#ifndef VIEWPORT_MANAGER_H
#define VIEWPORT_MANAGER_H

#include "repaint.h"

// Manages cached canvas composite, layer iteration, dirty tracking,
// and merge-down workflow.  Depends on Compositor + LayerStack.

extern bool layersDirty;

void ViewportManager_Init(void);
void ViewportManager_Shutdown(void);
void ViewportManager_ReloadShader(void);

// Canvas-resolution cached composite
RenderTexture2D* ViewportManager_Composite(void);
Image ViewportManager_CompositeWithDither(void);
void ViewportManager_CompositeViewInto(RenderTexture2D dst, const RectXform* viewXform, int w, int h);

void ViewportManager_SetDirty(void);

// Merge-down: blends top into bottom RT, handles RT replacement + layer delete + undo
void ViewportManager_MergeDown(int idx);
void ViewportManager_MergeDownSeamless(int idx);

#endif
