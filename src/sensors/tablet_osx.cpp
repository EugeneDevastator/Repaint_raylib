#include "tablet_platform.h"

#include <stdio.h>

int TabletPlatform_GetHookCount(void) { return 0; }

int TabletPlatform_DrainMousePos(float*, int) { return 0; }
void TabletPlatform_ClearMousePos(void) {}

void TabletPlatform_GetDebugInfo(char* buf, size_t sz) {
    snprintf(buf, sz, "macOS — no tablet backend yet");
}

bool TabletPlatform_Init(void* nativeWindow) {
    (void)nativeWindow;
    return false;
}

void TabletPlatform_Shutdown(void) {}

bool TabletPlatform_Poll(TabletState* state) {
    (void)state;
    return false;
}
