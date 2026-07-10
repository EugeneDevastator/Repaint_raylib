#include "platform_utils.h"
#include <cstddef>
#include <cstdlib>
#include <cstdio>

struct GLFWwindow;
extern "C" GLFWwindow* glfwGetCurrentContext(void);
extern "C" void* glfwGetWin32Window(GLFWwindow*);

void* Platform_GetNativeWindowHandle(void) {
    GLFWwindow* ctx = glfwGetCurrentContext();
    if (!ctx) return NULL;
    return (void*)glfwGetWin32Window(ctx);
}

void Platform_OpenURL(const char* url) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", url);
    system(cmd);
}
