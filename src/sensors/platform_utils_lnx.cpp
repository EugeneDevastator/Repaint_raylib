#include "platform_utils.h"
#include <cstddef>
#include <cstdlib>
#include <cstdio>

void* Platform_GetNativeWindowHandle(void) {
    return NULL;
}

void Platform_OpenURL(const char* url) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", url);
    system(cmd);
}
