#pragma once
#include "repaint.h"

void LeftPanel_Init(void);
void LeftPanel_Shutdown(void);
void LeftPanel_Draw(AppState* state);

// Quick panel sub-component visibility toggles (controlled from left panel)
extern bool g_showBrushPreview;
extern bool g_showStampPreview;
extern bool g_showTextureGroup;
extern bool g_showFilePanel;
extern bool g_showToolPanel;
