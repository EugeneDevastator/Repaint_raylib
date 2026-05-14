#include "repaint.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

#define TEX_DIM 512

void BrushTex_Init(AppState* state) {
    state->brushTexCount = 0;
    state->activeBrushTex = -1;
    state->editTexMode = 0;
    memset(state->brushTex, 0, sizeof(state->brushTex));

    const char* ad = GetApplicationDirectory();
    const char* defaultFiles[] = {"noise.png", "perlin.png", "clouds.png", "dots.png"};
    const char* defaultNames[] = {"Noise", "Perlin", "Clouds", "Dots"};
    int numDefaults = sizeof(defaultFiles) / sizeof(defaultFiles[0]);

    for (int i = 0; i < numDefaults; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%sresources/Noise/%s", ad, defaultFiles[i]);
        Image img;
        if (FileExists(path)) {
            img = LoadImage(path);
            ImageResize(&img, TEX_DIM, TEX_DIM);
        } else {
            img = GenImageColor(TEX_DIM, TEX_DIM, MAGENTA);
        }
        int idx = BrushTex_Add(state, defaultNames[i], img.width, img.height);
        BrushTexture* bt = &state->brushTex[idx];
        UnloadImage(bt->cpuImage);
        bt->cpuImage = img;
        bt->builtIn = true;
        bt->dirty = true;
    }

    BrushTex_SyncAll(state);
}

int BrushTex_Add(AppState* state, const char* name, int w, int h) {
    if (state->brushTexCount >= MAX_BRUSH_TEX) return -1;
    int idx = state->brushTexCount++;
    BrushTexture* bt = &state->brushTex[idx];
    bt->id = idx;
    snprintf(bt->name, sizeof(bt->name), "%s", name ? name : "Texture");
    bt->w = w;
    bt->h = h;
    bt->rt = Load16BitRT(w, h);
    BeginTextureMode(bt->rt);
    ClearBackground(BLANK);
    EndTextureMode();
    bt->cpuImage = GenImageColor(w, h, BLANK);
    bt->dirty = true;
    bt->builtIn = false;
    return idx;
}

void BrushTex_Delete(AppState* state, int idx) {
    if (idx < 0 || idx >= state->brushTexCount) return;
    BrushTexture* bt = &state->brushTex[idx];
    if (bt->builtIn) return;
    if (bt->rt.id > 0) UnloadRenderTexture(bt->rt);
    if (bt->cpuImage.data) UnloadImage(bt->cpuImage);
    // Shift remaining
    for (int i = idx; i < state->brushTexCount - 1; i++)
        state->brushTex[i] = state->brushTex[i + 1];
    state->brushTexCount--;
    if (state->activeBrushTex == idx) state->activeBrushTex = -1;
    else if (state->activeBrushTex > idx) state->activeBrushTex--;
}

void BrushTex_SetActive(AppState* state, int idx) {
    state->activeBrushTex = (idx >= 0 && idx < state->brushTexCount) ? idx : -1;
}

void BrushTex_SyncAll(AppState* state) {
    for (int i = 0; i < state->brushTexCount; i++) {
        BrushTexture* bt = &state->brushTex[i];
        if (!bt->dirty || !bt->cpuImage.data) continue;
        bt->dirty = false;
        // Upload CPU image to 16-bit RT
        Texture2D tmp = LoadTextureFromImage(bt->cpuImage);
        BeginTextureMode(bt->rt);
        ClearBackground(BLANK);
        DrawTexture(tmp, 0, 0, WHITE);
        EndTextureMode();
        UnloadTexture(tmp);
    }
}

Texture2D BrushTex_GetThumb(AppState* state, int idx) {
    // Return the GPU texture for thumb display
    if (idx < 0 || idx >= state->brushTexCount) return Texture2D{0};
    return state->brushTex[idx].rt.texture;
}
