#include "raylib.h"
#include "tablet_platform.h"
#include "platform_utils.h"
#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(700, 640, "Tablet Calibrator");
    SetTargetFPS(60);

    bool tabletOk = TabletPlatform_Init(Platform_GetNativeWindowHandle());

    TabletState state;
    float angle = 0.0f;
    int frames = 0;
    char debugInfo[4096] = "";
    while (!WindowShouldClose()) {
        TabletPlatform_Poll(&state);
        angle += 2.0f;
        frames++;

        BeginDrawing();
        ClearBackground((Color){30, 30, 30, 255});

        int sw = GetScreenWidth();
        int y = 16;
        int gap = 8;

        // Title
        DrawText("Tablet Calibrator", 20, y, 32, WHITE);
        {
            char fpsText[32];
            snprintf(fpsText, sizeof(fpsText), "FPS: %i", frames);
            int fpsW = MeasureText(fpsText, 22);
            DrawText(fpsText, sw - fpsW - 20, y, 22, LIME);
        }
        y += 32 + gap;

        // Tablet status
        DrawText(tabletOk ? "[OK] Tablet initialized" : "[FAIL] Tablet not detected",
                 20, y, 20, tabletOk ? GREEN : RED);
        y += 20 + gap;

        // Debug info (multi-line, variable height)
        TabletPlatform_GetDebugInfo(debugInfo, sizeof(debugInfo));
        {
            int lineCount = 1;
            for (char* p = debugInfo; *p; p++)
                if (*p == '\n') lineCount++;
            int dbgH = lineCount * 18;
            DrawText(debugInfo, 20, y, 16, LIGHTGRAY);
            y += dbgH + gap;
        }

        // State lines
        DrawText(TextFormat("%-12s %s", "Active:",   state.active   ? "YES" : "no"),  20, y, 22, WHITE);
        y += 24;
        DrawText(TextFormat("%-12s %s", "Touching:", state.touching ? "YES" : "no"),   20, y, 22, WHITE);
        y += 24;
        DrawText(TextFormat("%-12s %.4f", "Pressure:", state.pressure), 20, y, 22, WHITE);
        y += 24;
        DrawText(TextFormat("%-12s %+.4f", "Tilt X:",  state.tiltX), 20, y, 22, WHITE);
        y += 24;
        DrawText(TextFormat("%-12s %+.4f", "Tilt Y:",  state.tiltY), 20, y, 22, WHITE);
        y += 24;
        DrawText(TextFormat("%-12s %.4f", "Rotation:", state.rotation), 20, y, 22, WHITE);
        y += 24;

        // Buttons
        {
            char btnLine[128] = "Buttons:    ";
            int off = (int)strlen(btnLine);
            const char* names[3] = {"TIP", "BTN1", "BTN2"};
            for (int bi = 0; bi < 3; bi++)
                if (state.buttons & (1 << bi))
                    off += snprintf(btnLine + off, sizeof(btnLine) - off, "%s ", names[bi]);
            if (off == (int)strlen("Buttons:    "))
                snprintf(btnLine + off, sizeof(btnLine) - off, "(none)");
            DrawText(btnLine, 20, y, 22, YELLOW);
        }
        y += 24;

        // Win hook count
        if (tabletOk)
            DrawText(TextFormat("WM_POINTER msgs: %i", TabletPlatform_GetHookCount()), 20, y, 22, LIME);

        // Pressure bar
        {
            y += 24;
            int barW = 400;
            DrawRectangle(20, y, (int)(state.pressure * barW), 20, BLUE);
            DrawRectangleLines(20, y, barW, 20, LIGHTGRAY);
            y += 20 + gap;
        }

        // Spinning gizmo (top-right area)
        {
            Vector2 center = { (float)(sw - 50), 60.0f };
            float rad = angle * DEG2RAD;
            Vector2 pts[4];
            for (int i = 0; i < 4; i++) {
                float a = rad + i * 1.5708f;
                pts[i] = { center.x + std::cos(a) * 20, center.y + std::sin(a) * 20 };
            }
            DrawTriangle(pts[0], pts[1], pts[2], (Color){0, 200, 255, 200});
            DrawTriangle(pts[0], pts[2], pts[3], (Color){0, 200, 255, 200});
        }

        EndDrawing();
    }

    TabletPlatform_Shutdown();
    CloseWindow();
    return 0;
}
