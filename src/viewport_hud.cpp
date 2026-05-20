#include "repaint.h"
#include "rlgl.h"
#include <math.h>

extern Viewport viewport;
extern bool layersDirty;

// Staging RT for brush stamp preview (canvas-sized, lazy-allocated)
static RenderTexture2D g_stampStage = {0};
static int g_stageW = 0, g_stageH = 0;

// Preview throttling state
static int g_frameCounter = 0;
static const int g_previewUpdateInterval = 10; // update every 10 frames
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
    bool doStamp = quickPanelShow;

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

        g_frameCounter++;
        unsigned int currentHash = ComputeBrushHash(&state->currentBrush);
        bool paramsChanged = (currentHash != g_lastPreviewHash);

        if (paramsChanged || g_lastPreviewHash == 0 || (g_frameCounter % g_previewUpdateInterval) == 0) {
            // Copy composited document into staging RT
            BeginTextureMode(g_stampStage);
            ClearBackground(BLANK);
            DrawTextureRec(docBlendTex->texture, srcRect, Vector2{0, 0}, WHITE);

            // Use the same stroke engine as real painting
            {
                Texture2D bt = {0};
                if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
                    bt = state->brushTex[state->activeBrushTex].rt.texture;

                float cx = state->camera.target.x;
                float cy = state->camera.target.y;

                float spacingVal = GetModVal(&bpSpacing);
                float effectiveRadOut = state->currentBrush.Realb.rad_out;
                float spacing = fmaxf(effectiveRadOut * 2.0f * spacingVal, 1.0f);

                float maxSegLen = 200.0f;
                float segLen = fminf(effectiveRadOut * 8.0f, maxSegLen);
                if (segLen < spacing) segLen = spacing;

                // Diagonal to upper right corner
                float dirX = 1.0f;
                float dirY = -1.0f;
                float dirLen = sqrtf(dirX * dirX + dirY * dirY);
                dirX /= dirLen;
                dirY /= dirLen;

                int numDabs = (int)(segLen / spacing) + 1;
                if (numDabs < 2) numDabs = 2;

                // Build dabs using the same approach as the stroke engine
                struct PreviewDab {
                    float x, y;
                    float rad_out, rad_in, opacity, crv, x2y, resangle;
                    Color col;
                };
                PreviewDab dabs[256];

                for (int i = 0; i < numDabs && i < 256; i++) {
                    float dist = (float)i * spacing;
                    dabs[i].x = cx + dist * dirX;
                    dabs[i].y = cy + dist * dirY;

                    // Base brush values
                    dabs[i].rad_out = state->currentBrush.Realb.rad_out;
                    dabs[i].rad_in = state->currentBrush.Realb.rad_in;
                    dabs[i].opacity = state->currentBrush.Realb.opacity;
                    dabs[i].crv = state->currentBrush.Realb.crv;
                    dabs[i].x2y = state->currentBrush.Realb.x2y;
                    dabs[i].resangle = state->currentBrush.Realb.resangle;
                    dabs[i].col = state->currentBrush.Realb.col;
                }

                // Apply per-dab jitter (same as PerDabJitter in viewport.cpp)
                for (int i = 0; i < numDabs && i < 256; i++) {
                    float dr = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;

                    // Size jitter
                    { float d = dr * 2.0f * bpSize.user.jitter * (bpSize.outMax - bpSize.outMin);
                      dabs[i].rad_out += d; dabs[i].rad_out = fmaxf(bpSize.outMin, fminf(bpSize.outMax, dabs[i].rad_out)); }

                    // Hardness jitter
                    { float h = dabs[i].rad_in / fmaxf(dabs[i].rad_out, 0.001f);
                      float d = dr * 2.0f * bpHardness.user.jitter * (bpHardness.outMax - bpHardness.outMin);
                      h += d; h = fmaxf(0.0f, fminf(1.0f, h)); dabs[i].rad_in = dabs[i].rad_out * h; }

                    // Opacity jitter
                    { float d = dr * 2.0f * bpOpacity.user.jitter * (bpOpacity.outMax - bpOpacity.outMin);
                      dabs[i].opacity += d; dabs[i].opacity = fmaxf(0.0f, fminf(1.0f, dabs[i].opacity)); }

                    // SizeMul jitter
                    { float raw = BParam_GetValue(&bpSizeMul) + dr * 2.0f * bpSizeMul.user.jitter * (bpSizeMul.outMax - bpSizeMul.outMin);
                      raw = fmaxf(bpSizeMul.outMin, fminf(bpSizeMul.outMax, raw));
                      float f = powf(16.0f, raw / 128.0f - 1.0f);
                      dabs[i].rad_out *= f; dabs[i].rad_in *= f; }

                    // Scatter
                    float scatterVal = GetModVal(&bpScatter);
                    float scatterOffset = scatterVal * dabs[i].rad_out * 0.5f;
                    float perpX = -dirY;
                    float perpY = dirX;
                    float scatterDir = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f);
                    dabs[i].x += perpX * scatterDir * scatterOffset;
                    dabs[i].y += perpY * scatterDir * scatterOffset;
                }

                // Apply stamps
                for (int i = 0; i < numDabs && i < 256; i++) {
                    d_Brush dab = {};
                    dab.Realb.rad_out = dabs[i].rad_out;
                    dab.Realb.rad_in = dabs[i].rad_in;
                    dab.Realb.opacity = dabs[i].opacity;
                    dab.Realb.crv = dabs[i].crv;
                    dab.Realb.x2y = dabs[i].x2y;
                    dab.Realb.resangle = dabs[i].resangle;
                    dab.Realb.col = dabs[i].col;
                    dab.Realb.bmidx = state->currentBrush.Realb.bmidx;
                    dab.Realb.perspective = state->currentBrush.Realb.perspective;
                    dab.Realb.texScale = state->currentBrush.Realb.texScale;
                    dab.Realb.texFeather = state->currentBrush.Realb.texFeather;
                    dab.Realb.texThresh = state->currentBrush.Realb.texThresh;
                    dab.Realb.texBlendVal = state->currentBrush.Realb.texBlendVal;
                    dab.Realb.texBlendMode = state->currentBrush.Realb.texBlendMode;
                    dab.Realb.texNoisemode = state->currentBrush.Realb.texNoisemode;
                    dab.Realb.texColorMode = state->currentBrush.Realb.texColorMode;
                    dab.Realb.useTexLumAsAlpha = state->currentBrush.Realb.useTexLumAsAlpha;
                    dab.Realb.eraseMode = state->currentBrush.Realb.eraseMode;

                    BrushBlend_ApplyStamp(g_stampStage, &dab, bt, dabs[i].x, dabs[i].y, dabs[i].x, dabs[i].y);
                }
            }

            EndTextureMode();

            g_lastPreviewHash = currentHash;
        }

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
    g_lastPreviewHash = 0;
    g_frameCounter = 0;
}
