#include "tablet.h"
#include "tablet_platform.h"
#include "repaint.h"

static bool g_tabletOn = false;

bool Tablet_Init(void* nativeWindow) {
    g_tabletOn = TabletPlatform_Init(nativeWindow);
    g_modPars.Pars[csPressure] = 1.0f;
    return true;
}

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

    if (g_tabletOn)
        TabletPlatform_Poll(&state);

    g_modPars.Pars[csPressure] = state.pressure;
    g_modPars.Pars[csRot]     = state.rotation;
    g_modPars.Pars[csTilt]    = state.tiltX;
    g_modPars.Pars[csHtilt]   = state.tiltX;
    g_modPars.Pars[csVtilt]   = state.tiltY;
    g_modPars.Pars[csXtilt]   = state.tiltX;
    g_modPars.Pars[csYtilt]   = state.tiltY;
}
