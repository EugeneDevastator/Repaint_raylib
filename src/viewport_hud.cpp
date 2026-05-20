#include "repaint.h"
#include "rlgl.h"
#include "stroke_engine.h"
#include <math.h>

extern Viewport viewport;
extern bool layersDirty;

static RenderTexture2D g_stampStage = {0};
static int g_stageW = 0, g_stageH = 0;

static int g_frameCounter = 0;
static const int g_previewUpdateInterval = 10;
static unsigned int g_lastPreviewHash = 0;

static unsigned int ComputeBrushHash(d_Brush* b) {
    unsigned int h = 0;
    h ^= (unsigned int)(b->Realb.rad_out * 100);
    h ^= (unsigned int)(b->Realb.rad_in * 100) << 5;
    h ^= (unsigned int)(b->Realb.opacity * 100) << 10;
    h ^= (unsigned int)(b->Realb.crv * 100) << 15;
    h ^= (unsigned int)(b->Realb.x2y * 100) << 20;
    h ^= (unsigned int)(b->Realb.resangle) << 25;
    h ^= b->Realb.bmidx << 28;
    h ^= (unsigned int)(b->Realb.perspective * 100) << 12;
    h ^= (unsigned int)(BParam_GetValue(&bpSpacing) * 100) << 8;
    h ^= (unsigned int)(BParam_GetValue(&bpSizeMul) * 10) << 4;
    return h;
}

void ViewportHUD_Draw(AppState* state) {
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    Rectangle vpBounds = viewport.bounds;
    DrawRectangleRec(vpBounds, Color{55, 55, 55, 255});

    // Edit texture mode
    if (state->editTexMode && state->activeBrushTex >= 0 &&
        state->activeBrushTex < state->brushTexCount &&
        state->brushTex[state->activeBrushTex].rt.id > 0)
    {
        int tw = state->brushTex[state->activeBrushTex].w;
        int th = state->brushTex[state->activeBrushTex].h;
        float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
        float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
        float dstW = tw * state->camera.zoom;
        float dstH = th * state->camera.zoom;
        Rectangle dstRect = {dstX, dstY, dstW, dstH};

        Texture2D checker = {0};
        {
            Image img = GenImageColor(tw, th, BLANK);
            for (int y = 0; y < th; y += 8)
                for (int x = 0; x < tw; x += 8) {
                    bool light = ((x / 8) + (y / 8)) % 2 == 0;
                    Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
                    ImageDrawRectangle(&img, x, y, 8, 8, col);
                }
            checker = LoadTextureFromImage(img);
            UnloadImage(img);
        }
        DrawTexturePro(checker, Rectangle{0, 0, (float)tw, (float)th}, dstRect, Vector2{0, 0}, 0.0f, WHITE);
        UnloadTexture(checker);
        DrawTexturePro(state->brushTex[state->activeBrushTex].rt.texture,
            Rectangle{0, 0, (float)tw, (float)-th}, dstRect, Vector2{0, 0}, 0.0f, WHITE);
        return;
    }

    RenderTexture2D* docBlendTex = DocBlender_Composite(state);
    if (!docBlendTex || docBlendTex->id == 0) return;

    float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
    float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
    float dstW = cw * state->camera.zoom;
    float dstH = ch * state->camera.zoom;
    Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
    Rectangle dstRect = {dstX, dstY, dstW, dstH};

    bool doStamp = quickPanelShow;

    bool usePresent = GetPresentInited();
    if (usePresent) BeginShaderMode(GetPresentShader());

    if (doStamp) {
        if (g_stampStage.id == 0 || g_stageW != cw || g_stageH != ch) {
            if (g_stampStage.id > 0) UnloadRenderTexture(g_stampStage);
            g_stampStage = Load16BitRT(cw, ch);
            g_stageW = cw;
            g_stageH = ch;
        }

        g_frameCounter++;
        unsigned int currentHash = ComputeBrushHash(&state->currentBrush);
        bool paramsChanged = (currentHash != g_lastPreviewHash);

        if (paramsChanged || g_lastPreviewHash == 0 || (g_frameCounter % g_previewUpdateInterval) == 0) {
            BeginTextureMode(g_stampStage);
            ClearBackground(BLANK);
            DrawTextureRec(docBlendTex->texture, srcRect, Vector2{0, 0}, WHITE);

            Texture2D bt = {0};
            if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
                bt = state->brushTex[state->activeBrushTex].rt.texture;

            StrokeEngine_DrawPreview(g_stampStage, bt, &state->currentBrush.Realb,
                                     state->camera.target.x, state->camera.target.y);

            EndTextureMode();
            g_lastPreviewHash = currentHash;
        }

        DrawTexturePro(g_stampStage.texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
    } else {
        DrawTexturePro(docBlendTex->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
    }

    if (usePresent) EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_stampStage.id > 0) {
        UnloadRenderTexture(g_stampStage);
        g_stampStage = RenderTexture2D{0};
        g_stageW = 0;
        g_stageH = 0;
    }
    g_lastPreviewHash = 0;
    g_frameCounter = 0;
}
