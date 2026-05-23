#include "repaint.h"
#include "rlgl.h"
#include "stroke_engine.h"
#include <math.h>

#define PREVIEW_SZ 512

extern Viewport viewport;
extern bool layersDirty;

static RenderTexture2D g_previewBg  = {0};  // snapshot of canvas
static RenderTexture2D g_previewDst = {0};  // composited result (bg + brush)
static int g_frameCounter = 0;
static const int g_previewUpdateInterval = 10;
static unsigned int g_lastPreviewHash = 0;

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
    if (g_previewBg.id == 0) {
        g_previewBg  = Load16BitRT(PREVIEW_SZ, PREVIEW_SZ);
        g_previewDst = Load16BitRT(PREVIEW_SZ, PREVIEW_SZ);
    }
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
            float zoom  = state->camera.zoom;
            float cx    = state->camera.target.x;
            float cy    = state->camera.target.y;

            // Use a camera centered on target with zoom, positioned so that
            // (0,0) in the RT maps to the visible canvas area at the viewport center.
            Camera2D prevCam = {};
            prevCam.target   = Vector2{cx, cy};
            prevCam.offset   = Vector2{PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f};
            prevCam.zoom     = zoom;

            // Copy the visible canvas area into bg RT via the camera transform
            BeginTextureMode(g_previewBg);
            ClearBackground(BLANK);
            BeginMode2D(prevCam);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            DrawTextureRec(docBlendTex->texture,
                Rectangle{0, 0, (float)cw, (float)-ch},
                Vector2{0, 0}, WHITE);
            EndMode2D();
            EndTextureMode();

            // Scale brush radius by zoom so preview matches on-screen size
            d_RealBrush zoomBrush = state->currentBrush.Realb;
            zoomBrush.rad_out *= zoom;

            Texture2D bt = {0};
            if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
                bt = state->brushTex[state->activeBrushTex].rt.texture;

            BeginTextureMode(g_previewDst);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            DrawTextureRec(g_previewBg.texture,
                Rectangle{0, 0, (float)PREVIEW_SZ, (float)-PREVIEW_SZ},
                Vector2{0, 0}, WHITE);
            StrokeEngine_DrawPreview(g_previewDst, bt, &zoomBrush,
                                     PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f);
            EndTextureMode();

            g_lastPreviewHash = currentHash;
        }

        // Display the preview quad
        float hh = PREVIEW_SZ * 0.5f;
        float px = vpBounds.x + vpBounds.width * 0.5f - hh;
        float py = vpBounds.y + vpBounds.height * 0.5f - hh;
        DrawTexturePro(g_previewDst.texture,
            Rectangle{0, 0, (float)PREVIEW_SZ, (float)-PREVIEW_SZ},
            Rectangle{px, py, (float)PREVIEW_SZ, (float)PREVIEW_SZ},
            Vector2{0, 0}, 0.0f, WHITE);
    }

    rlSetBlendMode(RL_BLEND_ALPHA);
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_previewBg.id > 0)  { UnloadRenderTexture(g_previewBg);  g_previewBg  = RenderTexture2D{0}; }
    if (g_previewDst.id > 0) { UnloadRenderTexture(g_previewDst); g_previewDst = RenderTexture2D{0}; }
    g_lastPreviewHash = 0;
    g_frameCounter = 0;
}
