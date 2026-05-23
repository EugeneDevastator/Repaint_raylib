#include "platform_utils.h"
#include <cstddef>

struct GLFWwindow;
extern "C" GLFWwindow* glfwGetCurrentContext(void);
extern "C" void* glfwGetWin32Window(GLFWwindow*);

void* Platform_GetNativeWindowHandle(void) {
    GLFWwindow* ctx = glfwGetCurrentContext();
    if (!ctx) return NULL;
    return (void*)glfwGetWin32Window(ctx);
}
