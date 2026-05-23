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
            int lw = state->canvas.layerProps[i].layerW;
            int lh = state->canvas.layerProps[i].layerH;
            if (lw < 1) lw = state->canvas.width;
            if (lh < 1) lh = state->canvas.height;
            state->layerRTs[i] = Load16BitRT(lw, lh);
            SetTextureWrap(state->layerRTs[i].texture, TEXTURE_WRAP_REPEAT);
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
        SetTextureWrap(state->layerRTs[layer].texture, TEXTURE_WRAP_REPEAT);
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


// ── SyncImageFromRT ────────────────────────────────────────────────
// Read GPU render-target back to a CPU image (16-bit).
// raylib's LoadImageFromTexture may return R8G8B8A8 for any RT format,
// so we explicitly upscale back to R16G16B16A16 via ×257 if needed.
void SyncImageFromRT(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    if (state->layerRTs[layer].id == 0) return;
    Image cap = LoadImageFromTexture(state->layerRTs[layer].texture);
    ImageFlipVertical(&cap);
    if (cap.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16) {
        int px = cap.width * cap.height;
        uint8_t* src8 = (uint8_t*)cap.data;
        uint16_t* dst16 = (uint16_t*)malloc(px * 4 * sizeof(uint16_t));
        for (int i = 0; i < px; i++) {
            dst16[i*4]     = (uint16_t)src8[i*4]   * 257;
            dst16[i*4 + 1] = (uint16_t)src8[i*4+1] * 257;
            dst16[i*4 + 2] = (uint16_t)src8[i*4+2] * 257;
            dst16[i*4 + 3] = (uint16_t)src8[i*4+3] * 257;
        }
        free(cap.data);
        cap.data = dst16;
        cap.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    }
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
