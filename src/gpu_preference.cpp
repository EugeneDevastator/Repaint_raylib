#include "gpu_preference.h"
#include "raylib.h"

#if defined(_WIN32)
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 0x00000001;
}
#endif

#include "external/glad.h"

#include <stdio.h>

void GPU_Init(void) {
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    printf("GPU: vendor=\"%s\" renderer=\"%s\" version=\"%s\"\n",
           vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
    fflush(stdout);
}
