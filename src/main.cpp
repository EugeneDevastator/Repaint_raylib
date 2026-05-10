#include "repaint.h"
#include <pthread.h>

typedef struct GLFWwindow GLFWwindow;
extern "C" GLFWwindow* glfwGetCurrentContext(void);
extern "C" void glfwMakeContextCurrent(GLFWwindow*);

static pthread_t renderThread;
static pthread_mutex_t renderMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t renderCond = PTHREAD_COND_INITIALIZER;
static volatile bool framePending = false;
static volatile bool frameComplete = true;
static volatile bool running = true;
static GLFWwindow* mainWindow = NULL;

static void* RenderLoop(void* arg) {
    AppState* state = (AppState*)arg;

    glfwMakeContextCurrent(mainWindow);

    pthread_mutex_lock(&renderMutex);
    while (running) {
        while (!framePending && running)
            pthread_cond_wait(&renderCond, &renderMutex);
        if (!running) break;
        framePending = false;
        pthread_mutex_unlock(&renderMutex);

        App_Draw(state);

        pthread_mutex_lock(&renderMutex);
        frameComplete = true;
        pthread_cond_broadcast(&renderCond);
    }
    pthread_mutex_unlock(&renderMutex);

    glfwMakeContextCurrent(NULL);
    return NULL;
}

int main() {
    AppState state = {0};
    App_Init(&state);

    while (!WindowShouldClose()) {
        UpdateUI(&state);
        Viewport_HandleInput(&viewport, &state);
        App_Draw(&state);
    }

    App_Close(&state);
    return 0;
}
