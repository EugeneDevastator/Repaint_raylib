#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

// Platform-independent helper to retrieve the native window handle.
// On Windows: returns HWND from GLFW.
// On Linux: returns NULL (unused for tablet).
void* Platform_GetNativeWindowHandle(void);

// Open a URL in the default browser (platform-specific).
void Platform_OpenURL(const char* url);

#endif
