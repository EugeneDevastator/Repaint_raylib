#include "tablet.h"
#include "tablet_platform.h"
#include "repaint.h"

static bool g_tabletOn = false;
static bool g_tabletWasTouching = false;
static int  g_tabletButtons = 0;

// Last polled state — consumed by InputModulator
static TabletState g_lastTabletState = {};

bool Tablet_Init(void* nativeWindow) {
    g_tabletOn = TabletPlatform_Init(nativeWindow);
    return true;
}

bool Tablet_WasTouching(void) { return g_tabletWasTouching; }
int  Tablet_GetButtons(void)  { return g_tabletButtons; }
bool Tablet_IsOn(void)        { return g_tabletOn; }
TabletState Tablet_GetLastState(void) { return g_lastTabletState; }

void Tablet_Shutdown(void) {
    if (g_tabletOn)
        TabletPlatform_Shutdown();
    g_tabletOn = false;
}

void Tablet_UpdateModulators(void) {
    TabletState state;
    state.pressure = 1.0f;
    state.tiltX = 0.0f;
    state.tiltY = 0.0f;
    state.rotation = 0.5f;
    state.active = false;
    state.touching = false;
    state.buttons = 0;

    if (g_tabletOn)
        TabletPlatform_Poll(&state);

    g_tabletWasTouching = state.touching;
    g_tabletButtons = state.buttons;
    g_lastTabletState = state;
}
