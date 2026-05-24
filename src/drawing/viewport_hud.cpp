#include "repaint.h"
#include "rlgl.h"
#include "stroke_engine.h"
#include <math.h>

#define PREVIEW_SZ 512

extern Viewport viewport;
extern bool layersDirty;

static RenderTexture2D g_previewRT = {0};   // brush preview (strokes on transparent bg)
static int g_frameCounter = 0;
static const int g_previewUpdateInterval = 10;
static unsigned int g_lastPreviewHash = 0;

// Cached checkerboard texture for edit-texture backdrop.
// Created on demand, reused across frames — avoids creating + destroying
// a texture inside the draw loop, which could free the GL name before
// the batch flushes and cause the next draw to target a stale slot.
static Texture2D g_editCheckerTex = {0};
static int g_editCheckerW = 0, g_editCheckerH = 0;

static unsigned int ComputeBrushHash(d_Brush* b) {
    unsigned int h = 0;
    h ^= (unsigned int)(b->Realb.rad_out * 100);
    h ^= (unsigned int)(b->Realb.radInRatio * 100) << 5;
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

static void EnsurePreviewRTs(void) {
    if (g_previewRT.id == 0)
        g_previewRT = Load16BitRT(PREVIEW_SZ, PREVIEW_SZ);
}

void ViewportHUD_Draw(AppState* state) {
    int cw = state->doc.width;
    int ch = state->doc.height;
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

        // Cached checker backdrop — rebuild only if size changed
        if (g_editCheckerTex.id == 0 || g_editCheckerW != tw || g_editCheckerH != th) {
            if (g_editCheckerTex.id > 0) UnloadTexture(g_editCheckerTex);
            Image img = GenImageColor(tw, th, BLANK);
            for (int y = 0; y < th; y += 8)
                for (int x = 0; x < tw; x += 8) {
                    bool light = ((x / 8) + (y / 8)) % 2 == 0;
                    Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
                    ImageDrawRectangle(&img, x, y, 8, 8, col);
                }
            g_editCheckerTex = LoadTextureFromImage(img);
            UnloadImage(img);
            g_editCheckerW = tw;
            g_editCheckerH = th;
        }
        DrawTexturePro(g_editCheckerTex, Rectangle{0, 0, (float)tw, (float)th},
            dstRect, Vector2{0, 0}, 0.0f, WHITE);
        DrawTexturePro(state->brushTex[state->activeBrushTex].rt.texture,
            Rectangle{0, 0, (float)tw, (float)-th}, dstRect, Vector2{0, 0}, 0.0f, WHITE);
        return;
    }

    // Draw the canvas first (always)
    RenderTexture2D* docBlendTex = DocBlender_Composite(state);
    if (!docBlendTex || docBlendTex->id == 0) return;

    float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
    float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
    float dstW = cw * state->camera.zoom;
    float dstH = ch * state->camera.zoom;
    Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
    Rectangle dstRect = {dstX, dstY, dstW, dstH};

    bool usePresent = GetPresentInited();
    if (usePresent) BeginShaderMode(GetPresentShader());
    DrawTexturePro(docBlendTex->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
    if (usePresent) EndShaderMode();

    // ── Brush preview overlay (quick HUD) ────────────────────────────
    if (g_activeHud == HUD_QUICK) {
        EnsurePreviewRTs();

        g_frameCounter++;
        unsigned int currentHash = ComputeBrushHash(&state->currentBrush);
        bool paramsChanged = (currentHash != g_lastPreviewHash);

        if (paramsChanged || g_lastPreviewHash == 0 || (g_frameCounter % g_previewUpdateInterval) == 0) {
            // Scale brush radius by zoom so preview matches on-screen size
            d_RealBrush zoomBrush = state->currentBrush.Realb;
            zoomBrush.rad_out *= state->camera.zoom;

            Texture2D bt = {0};
            if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
                bt = state->brushTex[state->activeBrushTex].rt.texture;

            // Copy the visible canvas area as background (needed for smudge, harmless for paint)
            Camera2D prevCam = {};
            prevCam.target = state->camera.target;
            prevCam.offset = Vector2{PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f};
            prevCam.zoom   = state->camera.zoom;

            BeginTextureMode(g_previewRT);
            ClearBackground(BLANK);
            BeginMode2D(prevCam);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            DrawTextureRec(docBlendTex->texture,
                Rectangle{0, 0, (float)cw, (float)-ch},
                Vector2{0, 0}, WHITE);
            EndMode2D();
            // Draw preview strokes on top
            StrokeEngine_DrawPreview(g_previewRT, bt, &zoomBrush, state->mode,
                                     PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f);
            EndTextureMode();

            g_lastPreviewHash = currentHash;
        }

        // Display the preview quad
        float hh = PREVIEW_SZ * 0.5f;
        float px = vpBounds.x + vpBounds.width * 0.5f - hh;
        float py = vpBounds.y + vpBounds.height * 0.5f - hh;
        // Draw preview over the canvas — transparent bg lets canvas show through
        DrawTexturePro(g_previewRT.texture,
            Rectangle{0, 0, (float)PREVIEW_SZ, (float)-PREVIEW_SZ},
            Rectangle{px, py, (float)PREVIEW_SZ, (float)PREVIEW_SZ},
            Vector2{0, 0}, 0.0f, WHITE);
    }

    rlSetBlendMode(RL_BLEND_ALPHA);
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_previewRT.id > 0) { UnloadRenderTexture(g_previewRT); g_previewRT = RenderTexture2D{0}; }
    if (g_editCheckerTex.id > 0) { UnloadTexture(g_editCheckerTex); g_editCheckerTex = Texture2D{0}; }
    g_lastPreviewHash = 0;
    g_frameCounter = 0;
}
