#include "tablet_platform.h"
#include "platform_utils.h"
#include "raylib.h"
#include <cmath>

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(600, 540, "Tablet Calibrator");
    SetTargetFPS(60);

    bool tabletOk = TabletPlatform_Init(Platform_GetNativeWindowHandle());

    TabletState state;
    float angle = 0.0f;
    int frames = 0;

    while (!WindowShouldClose()) {
        TabletPlatform_Poll(&state);
        angle += 2.0f;
        frames++;

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

        DrawText("Tablet Calibrator", 20, 16, 32, WHITE);
        DrawText(TextFormat("FPS: %i", frames), 20, 54, 32, LIME);

        DrawText(tabletOk ? "[OK] Tablet initialized" : "[--] Tablet not found",
                 20, 92, 32, tabletOk ? GREEN : RED);

        DrawText(TextFormat("Active:   %s", state.active ? "YES" : "no"), 20, 136, 32, WHITE);
        DrawText(TextFormat("Touching: %s", state.touching ? "YES" : "no"), 20, 174, 32, WHITE);
        DrawText(TextFormat("Pressure: %.4f", state.pressure), 20, 212, 32, WHITE);
        DrawText(TextFormat("Tilt X:   %+.4f", state.tiltX), 20, 250, 32, WHITE);
        DrawText(TextFormat("Tilt Y:   %+.4f", state.tiltY), 20, 288, 32, WHITE);
        DrawText(TextFormat("Rotation: %.4f", state.rotation), 20, 326, 32, WHITE);

        DrawText(TextFormat("WM_POINTER msgs: %i", TabletPlatform_GetHookCount()), 20, 364, 32, LIME);

        DrawRectangle(20, 410, (int)(state.pressure * 400.0f), 20, BLUE);
        DrawRectangleLines(20, 410, 400, 20, LIGHTGRAY);

        Vector2 center = { 540, 60 };
        float rad = angle * DEG2RAD;
        Vector2 p1 = { center.x + std::cos(rad) * 20, center.y + std::sin(rad) * 20 };
        Vector2 p2 = { center.x + std::cos(rad + 1.5708f) * 20, center.y + std::sin(rad + 1.5708f) * 20 };
        Vector2 p3 = { center.x + std::cos(rad + 3.1416f) * 20, center.y + std::sin(rad + 3.1416f) * 20 };
        Vector2 p4 = { center.x + std::cos(rad + 4.7124f) * 20, center.y + std::sin(rad + 4.7124f) * 20 };
        DrawTriangle(p1, p2, p3, (Color){ 0, 200, 255, 200 });
        DrawTriangle(p1, p3, p4, (Color){ 0, 200, 255, 200 });

        EndDrawing();
    }

    TabletPlatform_Shutdown();
    CloseWindow();
    return 0;
}
