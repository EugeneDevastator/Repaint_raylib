#include "repaint.h"
#include "texture_manager.h"
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
    TM_Init();
    state->brushTexSlot = TM_INVALID_SLOT;
    state->editTexSlot = TM_INVALID_SLOT;
    state->brushTexActive = false;
    state->editTexMode = 0;

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
            if (!img.data) { printf("[BRUSHTEX] FAILED to load '%s'\n", path); continue; }
            printf("[BRUSHTEX] loaded '%s': %dx%d\n", texName, img.width, img.height);
            ImageResize(&img, TEX_DIM, TEX_DIM);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);

            TexSlotID id = BrushTex_Add(state, texName, img.width, img.height);
            if (TM_IsValid(id)) {
                TexSlot* ts = TM_Get(id);
                if (ts) {
                    ts->builtIn = true;
                    Texture2D tmp = LoadTextureFromImage(img);
                    if (id.slot == 0 && id.bucket == 0)
                        printf("[BRUSHTEX-RENDER] slot=0 tmp.id=%u texW=%d texH=%d\n",
                               tmp.id, tmp.width, tmp.height);
                    BeginTextureMode(ts->rt);
                    ClearBackground(BLANK);
                    rlSetBlendMode(RL_BLEND_CUSTOM);
                    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                    DrawTexture(tmp, 0, 0, WHITE);
                    rlSetBlendMode(RL_BLEND_ALPHA);
                    EndTextureMode();
                    UnloadTexture(tmp);
                }
            }
            UnloadImage(img);
        }
        closedir(d);
    }
}

TexSlotID BrushTex_Add(AppState* state, const char* name, int w, int h) {
    (void)state;
    return TM_Add(TM_BUCKET_USER, w, h, name, false);
}

void BrushTex_Delete(AppState* state, TexSlotID id) {
    if (!TM_IsValid(id)) return;
    TexSlot* ts = TM_Get(id);
    if (!ts || ts->builtIn) return;
    if (state->editTexSlot == id) {
        state->editTexSlot = TM_INVALID_SLOT;
        state->editTexMode = 0;
    }
    if (state->brushTexSlot == id) {
        state->brushTexSlot = TM_INVALID_SLOT;
        state->brushTexActive = false;
    }
    TM_Remove(id);
}

void BrushTex_SetActive(AppState* state, TexSlotID id) {
    if (!TM_IsValid(id)) {
        state->brushTexSlot = TM_INVALID_SLOT;
        state->brushTexActive = false;
    } else {
        state->brushTexSlot = id;
        state->brushTexActive = true;
    }
}

Texture2D BrushTex_GetThumb(AppState* state, TexSlotID id) {
    (void)state;
    TexSlot* ts = TM_Get(id);
    return ts ? ts->rt.texture : Texture2D{0};
}

TexSlotID BrushTex_GetSlot(AppState* state, int userTexSlot) {
    (void)state;
    if (userTexSlot <= 0) return TM_INVALID_SLOT;
    uint8_t slot = (uint8_t)(userTexSlot - 1);
    TexSlotID id = {TM_BUCKET_USER, slot};
    return TM_IsValid(id) ? id : TM_INVALID_SLOT;
}
