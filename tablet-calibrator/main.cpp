#include "raylib.h"
#include "tablet_platform.h"
#include "platform_utils.h"
#include <cmath>
#include <cstdio>

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(700, 640, "Tablet Calibrator");
    SetTargetFPS(60);

    bool tabletOk = TabletPlatform_Init(Platform_GetNativeWindowHandle());

    TabletState state;
    float angle = 0.0f;
    int frames = 0;
    char debugInfo[1024] = "";

    while (!WindowShouldClose()) {
        TabletPlatform_Poll(&state);
        angle += 2.0f;
        frames++;

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

        DrawText("Tablet Calibrator", 20, 16, 32, WHITE);
        DrawText(TextFormat("FPS: %i", frames), 20, 54, 22, LIME);

        if (tabletOk) {
            DrawText("[OK] Tablet initialized", 20, 88, 20, GREEN);
        } else {
            DrawText("[FAIL] Tablet not detected", 20, 88, 20, RED);
        }

        TabletPlatform_GetDebugInfo(debugInfo, sizeof(debugInfo));
        DrawText(debugInfo, 20, tabletOk ? 118 : 118, 16, LIGHTGRAY);

        float ly = tabletOk ? 270.0f : 270.0f;
        DrawText(TextFormat("Active:     %s", state.active   ? "YES" : "no"), 20, (int)ly,      22, WHITE);
        DrawText(TextFormat("Touching:   %s", state.touching ? "YES" : "no"), 20, (int)ly + 32,  22, WHITE);
        DrawText(TextFormat("Pressure:   %.4f", state.pressure), 20, (int)ly + 64,  22, WHITE);
        DrawText(TextFormat("Tilt X:     %+.4f", state.tiltX), 20, (int)ly + 96,  22, WHITE);
        DrawText(TextFormat("Tilt Y:     %+.4f", state.tiltY), 20, (int)ly + 128, 22, WHITE);
        DrawText(TextFormat("Rotation:   %.4f", state.rotation), 20, (int)ly + 160, 22, WHITE);

        if (tabletOk)
            DrawText(TextFormat("WM_POINTER msgs: %i", TabletPlatform_GetHookCount()), 20, (int)ly + 192, 22, LIME);

        float barY = ly + 230;
        DrawRectangle(20, (int)barY, (int)(state.pressure * 400.0f), 20, BLUE);
        DrawRectangleLines(20, (int)barY, 400, 20, LIGHTGRAY);

        Vector2 center = { 580, 60 };
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
