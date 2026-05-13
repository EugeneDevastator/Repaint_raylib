#include "repaint.h"
#include "rlgl.h"

static RenderTexture2D accumA = {0};
static RenderTexture2D accumB = {0};
static RenderTexture2D cleanComposite = {0};  // stamp-free composite snapshot
static bool accumInited = false;
static Texture2D checkerTex = {0};
static bool checkerValid = false;
static Shader layerBlendShader = {0};
static bool shaderInited = false;
static int locLayerTex = -1;
static int locLayerAlpha = -1;
static int locBmIdx = -1;
static int curCanvasW = 0;
static int curCanvasH = 0;
static RenderTexture2D* finalAcc = NULL;

bool layersDirty = true;

static void EnsureChecker(int w, int h) {
    if (checkerValid && checkerTex.width == w && checkerTex.height == h) return;
    if (checkerTex.id > 0) UnloadTexture(checkerTex);

    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y += 8) {
        for (int x = 0; x < w; x += 8) {
            bool light = ((x / 8) + (y / 8)) % 2 == 0;
            Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
            ImageDrawRectangle(&img, x, y, 8, 8, col);
        }
    }
    checkerTex = LoadTextureFromImage(img);
    UnloadImage(img);
    checkerValid = true;
}

static void EnsureAccumulators(int w, int h) {
    if (accumInited && curCanvasW == w && curCanvasH == h) return;
    if (accumInited) {
        UnloadRenderTexture(accumA);
        UnloadRenderTexture(accumB);
        if (cleanComposite.id > 0) UnloadRenderTexture(cleanComposite);
    }
    accumA = LoadRenderTexture(w, h);
    accumB = LoadRenderTexture(w, h);
    cleanComposite = LoadRenderTexture(w, h);
    curCanvasW = w;
    curCanvasH = h;
    accumInited = true;
    finalAcc = NULL;
    layersDirty = true;
}

static void EnsureShader(void) {
    if (shaderInited) return;
    const char* ad = GetApplicationDirectory();
    char fsPath[512];
    snprintf(fsPath, sizeof(fsPath), "%sshaders/layer_blend.fs", ad);
    layerBlendShader = LoadShader(0, fsPath);
    if (layerBlendShader.id == 0) {
        TraceLog(LOG_ERROR, "layer_blend.fs failed to load/compile");
        return;
    }
    locLayerTex   = GetShaderLocation(layerBlendShader, "layerTex");
    locLayerAlpha = GetShaderLocation(layerBlendShader, "layerAlpha");
    locBmIdx      = GetShaderLocation(layerBlendShader, "bmidx");
    TraceLog(LOG_INFO, "Shader locs: layerTex=%d alpha=%d bm=%d", locLayerTex, locLayerAlpha, locBmIdx);
    shaderInited = true;
}


void DrawViewport(AppState* state, Rectangle screenRect, Camera2D camera) {
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    EnsureAccumulators(cw, ch);
    EnsureChecker(cw, ch);
    EnsureShader();

    if (layersDirty) {
        RenderTexture2D* src = &accumA;
        RenderTexture2D* dst = &accumB;

        BeginTextureMode(*src);
        ClearBackground(BLANK);
        DrawTexture(checkerTex, 0, 0, WHITE);
        EndTextureMode();

        for (int i = 0; i < state->canvas.layerCount; i++) {
            if (!state->canvas.layerProps[i].visible) continue;
            if (!(state->texCount > i && state->layerRTs[i].id > 0)) continue;

            float alpha = state->canvas.layerProps[i].op;
            int bmidx = state->canvas.layerProps[i].blendmode;

            BeginTextureMode(*dst);
            ClearBackground(BLANK);

            if (shaderInited) {
                BeginShaderMode(layerBlendShader);

                SetShaderValueTexture(layerBlendShader, locLayerTex, state->layerRTs[i].texture);
                SetShaderValue(layerBlendShader, locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
                SetShaderValue(layerBlendShader, locBmIdx, &bmidx, SHADER_UNIFORM_INT);

                DrawTextureRec(src->texture,
                    Rectangle{0, 0, (float)cw, (float)-ch},
                    Vector2{0, 0}, WHITE);

                EndShaderMode();
            } else {
                DrawTextureRec(src->texture,
                    Rectangle{0, 0, (float)cw, (float)-ch},
                    Vector2{0, 0}, WHITE);
                DrawTextureRec(state->layerRTs[i].texture,
                    Rectangle{0, 0, (float)cw, (float)-ch},
                    Vector2{0, 0}, ColorAlpha(WHITE, alpha));
            }

            EndTextureMode();

            RenderTexture2D* tmp = src;
            src = dst;
            dst = tmp;
        }

        finalAcc = src;
        layersDirty = false;

        // Save clean composite (no stamp) for the preview overlay
        BeginTextureMode(cleanComposite);
        ClearBackground(BLANK);
        DrawTextureRec(finalAcc->texture,
            Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
        EndTextureMode();
    }

    if (!accumInited || finalAcc == NULL) return;

    // ── Brush preview stamp compositing (visual fake layer) ─────────
    extern bool quickPanelShow;
    if (quickPanelShow && shaderInited) {
        extern Viewport viewport;
        RenderTexture2D* overlay = (finalAcc == &accumA) ? &accumB : &accumA;

        // Copy clean composite (no stamp) into overlay
        BeginTextureMode(*overlay);
        ClearBackground(BLANK);
        DrawTextureRec(cleanComposite.texture,
            Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
        EndTextureMode();

        // Render brush stamp onto overlay with the actual blend mode
        d_Brush sb;
        memset(&sb, 0, sizeof(sb));
        sb.Realb = state->currentBrush.Realb;
        sb.Realb.opacity = 1.0f;

        Vector2 sc = {
            viewport.bounds.x + viewport.bounds.width * 0.5f,
            viewport.bounds.y + viewport.bounds.height * 0.5f
        };
        Vector2 cc = GetScreenToWorld2D(sc, camera);
        BrushBlend_ApplyStamp(*overlay, &sb, cc.x, cc.y, cc.x, cc.y);

        finalAcc = overlay;
    }

    int sw = (int)screenRect.width;
    int sh = (int)screenRect.height;
    if (sw < 1 || sh < 1) return;

    float dstX = -camera.target.x * camera.zoom + camera.offset.x;
    float dstY = -camera.target.y * camera.zoom + camera.offset.y;
    float dstW = cw * camera.zoom;
    float dstH = ch * camera.zoom;

    Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
    Rectangle dstRect = {dstX, dstY, dstW, dstH};
    DrawTexturePro(finalAcc->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
}

void ReloadViewportShader(void) {
    if (shaderInited) {
        UnloadShader(layerBlendShader);
        shaderInited = false;
        layersDirty = true;
    }
    EnsureShader();
    if (!shaderInited) TraceLog(LOG_WARNING, "ReloadViewportShader: layer_blend.fs still failed");
}

void MergeDownLayer(AppState* state, int idx) {
    if (idx <= 0 || idx >= state->canvas.layerCount) return;
    if (!shaderInited) return;

    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    // Ensure both layers have valid render textures
    if (state->texCount <= idx || state->layerRTs[idx].id == 0) return;
    if (state->texCount <= idx - 1 || state->layerRTs[idx - 1].id == 0) return;

    // Create temp RT, copy dest layer into it
    RenderTexture2D tempRT = LoadRenderTexture(cw, ch);
    BeginTextureMode(tempRT);
    ClearBackground(BLANK);
    DrawTextureRec(state->layerRTs[idx - 1].texture,
        Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndTextureMode();

    // Blend source layer onto tempRT using the same shader as viewport compositing
    float alpha = state->canvas.layerProps[idx].op;
    int bmidx = state->canvas.layerProps[idx].blendmode;
    BeginTextureMode(tempRT);
    BeginShaderMode(layerBlendShader);
    SetShaderValueTexture(layerBlendShader, locLayerTex, state->layerRTs[idx].texture);
    SetShaderValue(layerBlendShader, locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
    SetShaderValue(layerBlendShader, locBmIdx, &bmidx, SHADER_UNIFORM_INT);
    DrawTextureRec(state->layerRTs[idx - 1].texture,
        Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    // Read back to destination Image
    Image cap = LoadImageFromTexture(tempRT.texture);
    ImageFlipVertical(&cap);
    Image* dstImg = &state->canvas.layerImages[idx - 1];
    UnloadImage(*dstImg);
    *dstImg = cap;

    // Update destination RT
    BeginTextureMode(state->layerRTs[idx - 1]);
    ClearBackground(BLANK);
    DrawTextureRec(tempRT.texture, Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndTextureMode();
    UnloadRenderTexture(tempRT);

    // Remove source layer
    UnloadRenderTexture(state->layerRTs[idx]);
    Canvas_DeleteLayer(&state->canvas, idx);

    // Fix RT array: shift remaining entries down
    int n = state->canvas.layerCount;
    for (int i = idx; i < n; i++) {
        state->layerRTs[i] = state->layerRTs[i + 1];
        state->layerTextures[i] = state->layerTextures[i + 1];
    }
    state->layerRTs[n] = RenderTexture2D{0};
    state->layerTextures[n] = Texture2D{0};
    state->texCount = n;

    layersDirty = true;
}

void UnloadViewportRenderer(void) {
    if (accumInited) {
        UnloadRenderTexture(accumA);
        UnloadRenderTexture(accumB);
        if (cleanComposite.id > 0) UnloadRenderTexture(cleanComposite);
        accumInited = false;
    }
    if (checkerValid) {
        UnloadTexture(checkerTex);
        checkerValid = false;
    }
    if (shaderInited) {
        UnloadShader(layerBlendShader);
        shaderInited = false;
    }
}
