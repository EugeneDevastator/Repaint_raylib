#include "repaint.h"
#include "rlgl.h"

RenderTexture2D Load16BitRT(int width, int height) {
    RenderTexture2D target = { 0 };
    target.id = rlLoadFramebuffer();
    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
        target.texture.width = width;
        target.texture.height = height;
        target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        target.texture.mipmaps = 1;
        target.depth.id = rlLoadTextureDepth(width, height, true);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;
        target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
    }
    return target;
}

static RenderTexture2D accumA = {0};
static RenderTexture2D accumB = {0};
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
static Shader presentShader = {0};
static bool presentInited = false;

bool layersDirty = true;

bool GetPresentInited(void) { return presentInited; }
Shader GetPresentShader(void) { return presentShader; }

static void EnsurePresentShader(void);

Image Image_CompositeDithered(Image flat) {
    if (!flat.data || flat.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) return flat;
    EnsurePresentShader();
    if (!presentInited) return flat;

    int w = flat.width, h = flat.height;
    Texture2D flatTex = LoadTextureFromImage(flat);
    UnloadImage(flat);

    RenderTexture2D out = LoadRenderTexture(w, h);
    BeginTextureMode(out);
    ClearBackground(BLANK);
    BeginShaderMode(presentShader);
    DrawTextureRec(flatTex, (Rectangle){0, 0, (float)w, (float)-h}, (Vector2){0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();
    UnloadTexture(flatTex);

    Image result = LoadImageFromTexture(out.texture);
    ImageFlipVertical(&result);
    ImageFormat(&result, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(out);
    return result;
}

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
    }
    accumA = Load16BitRT(w, h);
    accumB = Load16BitRT(w, h);
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

RenderTexture2D* DocBlender_Composite(AppState* state) {
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return NULL;

    EnsureAccumulators(cw, ch);
    EnsureChecker(cw, ch);
    EnsureShader();
    EnsurePresentShader();

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
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
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
    }

    if (!accumInited || finalAcc == NULL) return NULL;
    return finalAcc;
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

static void EnsurePresentShader(void) {
    if (presentInited) return;
    const char* ad = GetApplicationDirectory();
    char fsPath[512];
    snprintf(fsPath, sizeof(fsPath), "%sshaders/present.fs", ad);
    presentShader = LoadShader(0, fsPath);
    presentInited = presentShader.id > 0;
    if (!presentInited) TraceLog(LOG_WARNING, "present.fs failed to load");
}

Image CompositeLayersWithDither(AppState* state) {
    EnsureShader();
    EnsurePresentShader();
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return {0};

    RenderTexture2D a = Load16BitRT(cw, ch);
    RenderTexture2D b = Load16BitRT(cw, ch);

    RenderTexture2D* src = &a;
    RenderTexture2D* dst = &b;

    // Composite all visible layers using the same shader as viewport
    BeginTextureMode(*src);
    ClearBackground(BLANK);
    for (int i = 0; i < state->canvas.layerCount; i++) {
        if (!state->canvas.layerProps[i].visible) continue;
        if (state->texCount <= i || state->layerRTs[i].id == 0) continue;

        float alpha = state->canvas.layerProps[i].op;
        int bmidx = state->canvas.layerProps[i].blendmode;

        BeginTextureMode(*dst);
        ClearBackground(BLANK);
        if (shaderInited) {
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            BeginShaderMode(layerBlendShader);
            SetShaderValueTexture(layerBlendShader, locLayerTex, state->layerRTs[i].texture);
            SetShaderValue(layerBlendShader, locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
            SetShaderValue(layerBlendShader, locBmIdx, &bmidx, SHADER_UNIFORM_INT);
            DrawTextureRec(src->texture, Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
            EndShaderMode();
        }
        EndTextureMode();

        RenderTexture2D* tmp = src; src = dst; dst = tmp;
    }

    // Apply present shader dither and read back as 8-bit
    BeginTextureMode(*dst);
    ClearBackground(BLANK);
    if (presentInited) BeginShaderMode(presentShader);
    DrawTextureRec(src->texture, Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    if (presentInited) EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);

    Image result = LoadImageFromTexture(dst->texture);
    ImageFlipVertical(&result);
    ImageFormat(&result, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    UnloadRenderTexture(a);
    UnloadRenderTexture(b);
    return result;
}

void MergeDownLayer(AppState* state, int idx) {
    if (idx <= 0 || idx >= state->canvas.layerCount) return;
    if (!shaderInited) return;

    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    if (state->texCount <= idx || state->layerRTs[idx].id == 0) return;
    if (state->texCount <= idx - 1 || state->layerRTs[idx - 1].id == 0) return;

    RenderTexture2D tempRT = LoadRenderTexture(cw, ch);
    BeginTextureMode(tempRT);
    ClearBackground(BLANK);
    DrawTextureRec(state->layerRTs[idx - 1].texture,
        Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndTextureMode();

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

    Image cap = LoadImageFromTexture(tempRT.texture);
    ImageFlipVertical(&cap);
    Image* dstImg = &state->canvas.layerImages[idx - 1];
    UnloadImage(*dstImg);
    *dstImg = cap;

    BeginTextureMode(state->layerRTs[idx - 1]);
    ClearBackground(BLANK);
    DrawTextureRec(tempRT.texture, Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndTextureMode();
    UnloadRenderTexture(tempRT);

    UnloadRenderTexture(state->layerRTs[idx]);
    Canvas_DeleteLayer(&state->canvas, idx);

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
    if (presentInited) {
        UnloadShader(presentShader);
        presentInited = false;
    }
}
