#ifndef TABLET_H
#define TABLET_H

#include <stdint.h>

// Platform-independent tablet state
struct TabletState {
    float pressure;   // 0.0 – 1.0
    float tiltX;      // -1.0 – 1.0
    float tiltY;      // -1.0 – 1.0
    float rotation;   // 0.0 – 1.0 (normalized 0–360°)
    bool active;      // pen is in proximity / touching
    bool touching;    // pen is pressing on surface
};

// nativeWindow: platform-specific handle (HWND on Windows, NULL on Linux)
bool Tablet_Init(void* nativeWindow);
void Tablet_Shutdown(void);
bool Tablet_Poll(TabletState* out);

// Called each frame to update g_modPars with tablet state
void Tablet_UpdateModulators(void);

#endif
