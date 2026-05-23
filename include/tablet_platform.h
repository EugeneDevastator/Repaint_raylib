#ifndef TABLET_PLATFORM_H
#define TABLET_PLATFORM_H

#include "tablet.h"

// Platform-specific tablet backend (implemented per-platform)
// nativeWindow: platform-specific window handle (e.g. HWND on Windows)
bool TabletPlatform_Init(void* nativeWindow);
void TabletPlatform_Shutdown(void);
bool TabletPlatform_Poll(TabletState* out);

// Debug diagnostics (platform-independent)
int TabletPlatform_GetHookCount(void);
void TabletPlatform_GetDebugInfo(char* buf, size_t sz);

#endif
