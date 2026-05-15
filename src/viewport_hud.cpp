#include "repaint.h"
#include "rlgl.h"
#include <math.h>

extern Viewport viewport;
extern bool layersDirty;
extern bool g_showStampPreview;

// Staging RT for brush stamp preview (canvas-sized, lazy-allocated)
static RenderTexture2D g_stampStage = {0};
static int g_stageW = 0, g_stageH = 0;

void ViewportHUD_Draw(AppState* state) {
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    Rectangle vpBounds = viewport.bounds;

    // ── Background ───────────────────────────────────────────────────
    DrawRectangleRec(vpBounds, Color{55, 55, 55, 255});

    // ── Edit texture mode: draw checker + texture directly ──────────
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

        // Checker background
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
        DrawTexturePro(checker,
            Rectangle{0, 0, (float)tw, (float)th},
            dstRect, Vector2{0, 0}, 0.0f, WHITE);
        UnloadTexture(checker);

        // Brush texture
        DrawTexturePro(state->brushTex[state->activeBrushTex].rt.texture,
            Rectangle{0, 0, (float)tw, (float)-th},
            dstRect, Vector2{0, 0}, 0.0f, WHITE);
        return;
    }

    // ── Composite layers ────────────────────────────────────────────
    RenderTexture2D* docBlendTex = DocBlender_Composite(state);
    if (!docBlendTex || docBlendTex->id == 0) return;

    // ── Screen coordinates (pan/zoom transform) ─────────────────────
    float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
    float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
    float dstW = cw * state->camera.zoom;
    float dstH = ch * state->camera.zoom;
    Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
    Rectangle dstRect = {dstX, dstY, dstW, dstH};

    // ── Optional brush stamp preview ────────────────────────────────
    bool doStamp = quickPanelShow && g_showStampPreview;

    // Present shader (dither) applied during screen draw
    bool usePresent = GetPresentInited();
    if (usePresent) BeginShaderMode(GetPresentShader());

    if (doStamp) {
        // Ensure staging RT is canvas-sized
        if (g_stampStage.id == 0 || g_stageW != cw || g_stageH != ch) {
            if (g_stampStage.id > 0) UnloadRenderTexture(g_stampStage);
            g_stampStage = Load16BitRT(cw, ch);
            g_stageW = cw;
            g_stageH = ch;
        }

        // Copy composited document into staging RT
        BeginTextureMode(g_stampStage);
        ClearBackground(BLANK);
        DrawTextureRec(docBlendTex->texture, srcRect, Vector2{0, 0}, WHITE);
        EndTextureMode();

        // Render brush stamp on top (uses BrushBlend_ApplyStamp internally)
        DrawStamp(g_stampStage, state);

        // Draw stamped result to screen
        DrawTexturePro(g_stampStage.texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
    } else {
        // Draw doc directly to screen
        DrawTexturePro(docBlendTex->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
    }

    if (usePresent) EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);

    // ── Debug overlays (stroke stamp positions) ──────────────────
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_stampStage.id > 0) {
        UnloadRenderTexture(g_stampStage);
        g_stampStage = RenderTexture2D{0};
        g_stageW = 0;
        g_stageH = 0;
    }
}
