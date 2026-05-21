#ifndef TABLET_PLATFORM_H
#define TABLET_PLATFORM_H

#include "tablet.h"

// Platform-specific tablet backend (implemented per-platform)
// nativeWindow: platform-specific window handle (e.g. HWND on Windows)
bool TabletPlatform_Init(void* nativeWindow);
void TabletPlatform_Shutdown(void);
bool TabletPlatform_Poll(TabletState* out);

// Debug: number of WM_POINTER messages intercepted this session
int TabletPlatform_GetHookCount(void);
int TabletPlatform_GetPenSuccess(void);
int TabletPlatform_GetTypeMismatch(void);
int TabletPlatform_GetLastType(void);

#endif
