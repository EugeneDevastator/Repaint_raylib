#include "platform_utils.h"
#include <cstddef>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

void* Platform_GetNativeWindowHandle(void) {
    GLFWwindow* ctx = glfwGetCurrentContext();
    if (!ctx) return NULL;
    return (void*)glfwGetWin32Window(ctx);
}
