#include "repaint.h"
#include "rlgl.h"
#include "stroke_engine.h"
#include <math.h>

#define PREVIEW_SZ 512

extern Viewport viewport;
extern bool layersDirty;
extern bool g_seamlessPreview;
extern bool g_useViewRes;

// Cached viewport-resolution render target for "Use screen res" mode
static RenderTexture2D g_viewResRT = {0};

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

    bool usePresent = GetPresentInited();
    RenderTexture2D* docBlendTex = NULL;
    if (!state->editTexMode) {

    if (g_useViewRes) {
        // Viewport-sized RT — recreate only when viewport changes size
        int vpW = (int)vpBounds.width, vpH = (int)vpBounds.height;
        if (g_viewResRT.id == 0 || g_viewResRT.texture.width != (unsigned)vpW || g_viewResRT.texture.height != (unsigned)vpH)
            g_viewResRT = Load16BitRT(vpW, vpH);
        if (vpW < 1 || vpH < 1) return;

        float vOffX = state->camera.offset.x - vpBounds.x;
        float vOffY = state->camera.offset.y - vpBounds.y;
        float vMat[6] = {state->camera.zoom, 0,
            -state->camera.target.x * state->camera.zoom + vOffX, 0,
            state->camera.zoom,
            -state->camera.target.y * state->camera.zoom + vOffY};
        LayerStack_ProduceCompositeView(g_viewResRT, vMat, vpW, vpH);
        if (usePresent) { BeginShaderMode(GetPresentShader()); LayerStack_SetPresentTexSize(vpW, vpH); LayerStack_SetPresentDither(true); }
        DrawTextureRec(g_viewResRT.texture,
            Rectangle{0, 0, (float)vpW, (float)-vpH},
            Vector2{vpBounds.x, vpBounds.y}, WHITE);
        if (usePresent) EndShaderMode();
        docBlendTex = &g_viewResRT;
    } else {
        // Legacy: composite at canvas resolution, draw at camera position
        docBlendTex = DocBlender_Composite(state);
        if (!docBlendTex || docBlendTex->id == 0) return;

        float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
        float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
        float dstW = cw * state->camera.zoom;
        float dstH = ch * state->camera.zoom;
        Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
        Rectangle dstRect = {dstX, dstY, dstW, dstH};

        if (g_seamlessPreview) {
            SetTextureWrap(docBlendTex->texture, TEXTURE_WRAP_REPEAT);
            if (usePresent) { BeginShaderMode(GetPresentShader()); LayerStack_SetPresentTexSize(cw, ch); LayerStack_SetPresentDither(true); }
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    DrawTexturePro(docBlendTex->texture, srcRect,
                        Rectangle{dstX + dx * dstW, dstY + dy * dstH, dstW, dstH},
                        Vector2{0, 0}, 0.0f, WHITE);
            if (usePresent) EndShaderMode();
        } else {
            if (usePresent) { BeginShaderMode(GetPresentShader()); LayerStack_SetPresentTexSize(cw, ch); LayerStack_SetPresentDither(true); }
            DrawTexturePro(docBlendTex->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
            if (usePresent) EndShaderMode();
        }
    }
    }

    // ── Brush preview overlay (quick HUD) ────────────────────────────
    if (g_activeHud == HUD_QUICK) {
        EnsurePreviewRTs();

        g_frameCounter++;
        unsigned int currentHash = ComputeBrushHash(&state->currentBrush);
        bool paramsChanged = (currentHash != g_lastPreviewHash);

        if (paramsChanged || g_lastPreviewHash == 0 || (g_frameCounter % g_previewUpdateInterval) == 0) {
            d_RealBrush zoomBrush = state->currentBrush.Realb;

            Texture2D bt = {0};
            if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
                bt = state->brushTex[state->activeBrushTex].rt.texture;

            // Copy the visible canvas area as background (needed for smudge, harmless for paint)
            BeginTextureMode(g_previewRT);
            ClearBackground(BLANK);
            if (!g_useViewRes) {
                Camera2D prevCam = {};
                prevCam.target = state->camera.target;
                prevCam.offset = Vector2{PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f};
                prevCam.zoom   = 1.0f;
                BeginMode2D(prevCam);
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                DrawTextureRec(docBlendTex->texture,
                    Rectangle{0, 0, (float)cw, (float)-ch},
                    Vector2{0, 0}, WHITE);
                EndMode2D();
            }
            // Draw preview strokes on top
            StrokeEngine_DrawPreview(g_previewRT, bt, &zoomBrush, state->mode,
                                     PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f);
            EndTextureMode();

            g_lastPreviewHash = currentHash;
        }

        // Display the preview quad — scale by zoom so brush size matches viewport canvas
        float previewScreenSz = (float)PREVIEW_SZ * state->camera.zoom;
        float hh = previewScreenSz * 0.5f;
        float px = vpBounds.x + vpBounds.width * 0.5f - hh;
        float py = vpBounds.y + vpBounds.height * 0.5f - hh;
        DrawTexturePro(g_previewRT.texture,
            Rectangle{0, 0, (float)PREVIEW_SZ, (float)-PREVIEW_SZ},
            Rectangle{px, py, previewScreenSz, previewScreenSz},
            Vector2{0, 0}, 0.0f, WHITE);
    }

    rlSetBlendMode(RL_BLEND_ALPHA);
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_previewRT.id > 0) { UnloadRenderTexture(g_previewRT); g_previewRT = RenderTexture2D{0}; }
    if (g_editCheckerTex.id > 0) { UnloadTexture(g_editCheckerTex); g_editCheckerTex = Texture2D{0}; }
    if (g_viewResRT.id > 0) { UnloadRenderTexture(g_viewResRT); g_viewResRT = RenderTexture2D{0}; }
    g_lastPreviewHash = 0;
    g_frameCounter = 0;
}
