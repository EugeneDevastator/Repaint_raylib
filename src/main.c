#include "repaint.h"

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
