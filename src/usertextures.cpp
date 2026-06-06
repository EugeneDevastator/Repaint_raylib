#include "repaint.h"
#include "raylib.h"

Texture2D g_activeBrushTex = {0};
Texture2D g_defaultBrushTex = {0};

void UserTexture_Init(void) {
    Image img = GenImageColor(16, 16, WHITE);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    g_defaultBrushTex = LoadTextureFromImage(img);
    UnloadImage(img);
    g_activeBrushTex = g_defaultBrushTex;
}

void UserTexture_Shutdown(void) {
    if (g_defaultBrushTex.id > 0) UnloadTexture(g_defaultBrushTex);
    g_defaultBrushTex = (Texture2D){0};
    g_activeBrushTex = (Texture2D){0};
}

void UserTexture_Update(AppState* state) {
    if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount) {
        g_activeBrushTex = state->brushTex[state->activeBrushTex].rt.texture;
    } else {
        g_activeBrushTex = g_defaultBrushTex;
    }
}
