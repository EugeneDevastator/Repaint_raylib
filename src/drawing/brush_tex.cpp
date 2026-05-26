#include "repaint.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>
#include <dirent.h>

#define TEX_DIM 512

static bool hasSuffix(const char* str, const char* suffix) {
    size_t slen = strlen(str), suflen = strlen(suffix);
    if (slen < suflen) return false;
    return strcasecmp(str + slen - suflen, suffix) == 0;
}

void BrushTex_Init(AppState* state) {
    state->brushTexCount = 0;
    state->activeBrushTex = -1;
    state->editTexMode = 0;
    memset(state->brushTex, 0, sizeof(state->brushTex));

    const char* ad = GetApplicationDirectory();
    char noiseDir[1024];
    snprintf(noiseDir, sizeof(noiseDir), "%sresources/Noise", ad);

    DIR* d = opendir(noiseDir);
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL) {
            if (!hasSuffix(entry->d_name, ".png")) continue;

            char texName[64];
            size_t len = strlen(entry->d_name);
            size_t stemLen = len - 4;
            if (stemLen >= sizeof(texName)) stemLen = sizeof(texName) - 1;
            memcpy(texName, entry->d_name, stemLen);
            texName[stemLen] = '\0';

            char path[1536];
            snprintf(path, sizeof(path), "%s/%s", noiseDir, entry->d_name);

            Image img = LoadImage(path);
            ImageResize(&img, TEX_DIM, TEX_DIM);

            int idx = BrushTex_Add(state, texName, img.width, img.height);
            if (idx >= 0) {
                BrushTexture* bt = &state->brushTex[idx];
                UnloadImage(bt->cpuImage);
                bt->cpuImage = img;
                bt->dirty = true;
            } else {
                UnloadImage(img);
            }
        }
        closedir(d);
    }

    // Mark all textures loaded from Noise/ as bundled
    for (int i = 0; i < state->brushTexCount; i++)
        state->brushTex[i].builtIn = true;

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
    bt->cpuImage = GenImageColor(w, h, BLANK);
    // Upload blank CPU image to RT — this properly initialises the GPU texture
    // even if ClearBackground + BeginTextureMode target an incomplete FBO.
    bt->dirty = true;
    bt->builtIn = false;
    if (bt->rt.id > 0) {
        Texture2D blank = LoadTextureFromImage(bt->cpuImage);
        BeginTextureMode(bt->rt);
        ClearBackground(BLANK);
        DrawTexture(blank, 0, 0, WHITE);
        EndTextureMode();
        UnloadTexture(blank);
        bt->dirty = false;
    }
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
