#include "repaint.h"
#include "rlgl.h"

void EnsureRTs(AppState* state) {
    int newCount = state->canvas.layerCount;
    if (state->texCount == newCount) return;
    int old = state->texCount;
    state->layerRTs = (RenderTexture2D*)realloc(state->layerRTs, newCount * sizeof(RenderTexture2D));
    state->layerTextures = (Texture2D*)realloc(state->layerTextures, newCount * sizeof(Texture2D));
    state->texDirty = (bool*)realloc(state->texDirty, newCount * sizeof(bool));
    if (newCount > old) {
        memset(&state->layerRTs[old], 0, (newCount - old) * sizeof(RenderTexture2D));
        memset(&state->layerTextures[old], 0, (newCount - old) * sizeof(Texture2D));
        for (int i = old; i < newCount; i++) {
            state->layerRTs[i] = Load16BitRT(state->canvas.width, state->canvas.height);
            BeginTextureMode(state->layerRTs[i]);
            ClearBackground(BLANK);
            EndTextureMode();
        }
    }
    state->texCount = newCount;
}

void SyncRTFromImage(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    Image* img = &state->canvas.layerImages[layer];
    if (state->layerRTs[layer].id == 0) {
        state->layerRTs[layer] = Load16BitRT(img->width, img->height);
    }
    Texture2D tmp = LoadTextureFromImage(*img);
    BeginTextureMode(state->layerRTs[layer]);
    ClearBackground(BLANK);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);
}

void SyncImageFromRT(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    if (state->layerRTs[layer].id == 0) return;
    Image cap = LoadImageFromTexture(state->layerRTs[layer].texture);
    ImageFlipVertical(&cap);
    ImageFormat(&cap, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Image* dst = &state->canvas.layerImages[layer];
    UnloadImage(*dst);
    *dst = cap;
}

void SyncAllImages(AppState* state) {
    for (int i = 0; i < state->texCount; i++)
        SyncImageFromRT(state, i);
}

void SyncAllRTs(AppState* state) {
    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++) {
        SyncRTFromImage(state, i);
        if (state->layerTextures[i].id > 0) UnloadTexture(state->layerTextures[i]);
        state->layerTextures[i] = LoadTextureFromImage(state->canvas.layerImages[i]);
    }
}

void SyncLayerTexture(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    if (state->layerRTs[layer].id == 0) return;
    SyncImageFromRT(state, layer);
    if (state->layerTextures[layer].id > 0) UnloadTexture(state->layerTextures[layer]);
    state->layerTextures[layer] = LoadTextureFromImage(state->canvas.layerImages[layer]);
}
