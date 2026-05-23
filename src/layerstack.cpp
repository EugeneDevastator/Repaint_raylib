#include "layerstack.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    int cw, ch;
    AppState* app;
    RenderTexture2D accumA, accumB, layerTransRT;
    bool accumInited;
    Texture2D checkerTex;
    bool checkerValid;
    Shader blendShader;
    bool shaderInited;
    int locLayerTex, locLayerAlpha, locBmIdx, locLayerThreshold, locLayerFeather;
    Shader presentShader;
    bool presentInited;
    int curCanvasW, curCanvasH;
    RenderTexture2D* finalAcc;
    bool dirty;
} LS = {0};

static void EnsurePresentShader(void);

// ── RT helpers ───────────────────────────────────────────────────────
RenderTexture2D Load16BitRT(int w, int h) {
    RenderTexture2D target = {0};
    target.id = rlLoadFramebuffer();
    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        target.texture.id = rlLoadTexture(NULL, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
        target.texture.width = w;
        target.texture.height = h;
        target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        target.texture.mipmaps = 1;
        target.depth.id = rlLoadTextureDepth(w, h, true);
        target.depth.width = w;
        target.depth.height = h;
        target.depth.format = 19;
        target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
    }
    return target;
}

// ── Init / shutdown ──────────────────────────────────────────────────
void LayerStack_Init(int canvasW, int canvasH) {
    LS.cw = canvasW;
    LS.ch = canvasH;
    LS.app = NULL;
    LS.accumInited = false;
    LS.checkerValid = false;
    LS.shaderInited = false;
    LS.presentInited = false;
    LS.finalAcc = NULL;
    LS.dirty = true;
}

void LayerStack_Bind(AppState* state) {
    LS.app = state;
    LS.cw = state->canvas.width;
    LS.ch = state->canvas.height;
    LS.dirty = true;
}

void LayerStack_Shutdown(void) {
    if (LS.accumInited) {
        UnloadRenderTexture(LS.accumA);
        UnloadRenderTexture(LS.accumB);
        UnloadRenderTexture(LS.layerTransRT);
        LS.accumInited = false;
    }
    if (LS.checkerValid) {
        UnloadTexture(LS.checkerTex);
        LS.checkerValid = false;
    }
    if (LS.shaderInited) {
        UnloadShader(LS.blendShader);
        LS.shaderInited = false;
    }
    if (LS.presentInited) {
        UnloadShader(LS.presentShader);
        LS.presentInited = false;
    }
    LS.app = NULL;
}

void LayerStack_ReloadShader(void) {
    if (LS.shaderInited) {
        UnloadShader(LS.blendShader);
        LS.shaderInited = false;
        LS.dirty = true;
    }
    const char* ad = GetApplicationDirectory();
    char fs[512];
    snprintf(fs, sizeof(fs), "%sshaders/layer_blend.fs", ad);
    LS.blendShader = LoadShader(0, fs);
    if (LS.blendShader.id == 0) { TraceLog(LOG_ERROR, "layer_blend.fs failed"); return; }
    LS.locLayerTex   = GetShaderLocation(LS.blendShader, "layerTex");
    LS.locLayerAlpha = GetShaderLocation(LS.blendShader, "layerAlpha");
    LS.locBmIdx      = GetShaderLocation(LS.blendShader, "bmidx");
    LS.locLayerThreshold = GetShaderLocation(LS.blendShader, "layerThreshold");
    LS.locLayerFeather   = GetShaderLocation(LS.blendShader, "layerFeather");
    LS.shaderInited = true;
}

// ── Query ────────────────────────────────────────────────────────────
int  LayerStack_Count(void) { return LS.app ? LS.app->canvas.layerCount : 0; }
int  LayerStack_Width(void) { return LS.cw; }
int  LayerStack_Height(void) { return LS.ch; }
bool LayerStack_Dirty(void) { return LS.dirty; }

// ── Data access ──────────────────────────────────────────────────────
sLayerProps*    LayerStack_GetProps(int idx) { return (LS.app && idx >= 0 && idx < LS.app->canvas.layerCount) ? &LS.app->canvas.layerProps[idx] : NULL; }
Image*          LayerStack_GetImage(int idx) { return (LS.app && idx >= 0 && idx < LS.app->canvas.layerCount) ? &LS.app->canvas.layerImages[idx] : NULL; }
RenderTexture2D LayerStack_GetRT(int idx)    { return (LS.app && idx >= 0 && idx < LS.app->texCount) ? LS.app->layerRTs[idx] : RenderTexture2D{0}; }
Texture2D       LayerStack_GetTex(int idx)   { return (LS.app && idx >= 0 && idx < LS.app->texCount) ? LS.app->layerTextures[idx] : Texture2D{0}; }

// ── Ensure GPU resources ────────────────────────────────────────────
static void EnsureAccumulators(int w, int h) {
    if (LS.accumInited && LS.curCanvasW == w && LS.curCanvasH == h) return;
    if (LS.accumInited) {
        UnloadRenderTexture(LS.accumA);
        UnloadRenderTexture(LS.accumB);
        UnloadRenderTexture(LS.layerTransRT);
    }
    LS.accumA = Load16BitRT(w, h);
    LS.accumB = Load16BitRT(w, h);
    LS.layerTransRT = Load16BitRT(w, h);
    SetTextureWrap(LS.accumA.texture, TEXTURE_WRAP_REPEAT);
    SetTextureWrap(LS.accumB.texture, TEXTURE_WRAP_REPEAT);
    SetTextureWrap(LS.layerTransRT.texture, TEXTURE_WRAP_REPEAT);
    LS.curCanvasW = w;
    LS.curCanvasH = h;
    LS.accumInited = true;
    LS.finalAcc = NULL;
    LS.dirty = true;
}

static void EnsureChecker(int w, int h) {
    if (LS.checkerValid && LS.checkerTex.width == w && LS.checkerTex.height == h) return;
    if (LS.checkerTex.id > 0) UnloadTexture(LS.checkerTex);
    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y += 8)
        for (int x = 0; x < w; x += 8) {
            bool light = ((x / 8) + (y / 8)) % 2 == 0;
            Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
            ImageDrawRectangle(&img, x, y, 8, 8, col);
        }
    LS.checkerTex = LoadTextureFromImage(img);
    UnloadImage(img);
    LS.checkerValid = true;
}

static void EnsureShader(void) {
    if (LS.shaderInited) return;
    const char* ad = GetApplicationDirectory();
    char fs[512];
    snprintf(fs, sizeof(fs), "%sshaders/layer_blend.fs", ad);
    LS.blendShader = LoadShader(0, fs);
    if (LS.blendShader.id == 0) { TraceLog(LOG_ERROR, "layer_blend.fs failed"); return; }
    LS.locLayerTex   = GetShaderLocation(LS.blendShader, "layerTex");
    LS.locLayerAlpha = GetShaderLocation(LS.blendShader, "layerAlpha");
    LS.locBmIdx      = GetShaderLocation(LS.blendShader, "bmidx");
    LS.locLayerThreshold = GetShaderLocation(LS.blendShader, "layerThreshold");
    LS.locLayerFeather   = GetShaderLocation(LS.blendShader, "layerFeather");
    LS.shaderInited = true;
}

static void EnsurePresentShader(void) {
    if (LS.presentInited) return;
    const char* ad = GetApplicationDirectory();
    char fs[512];
    snprintf(fs, sizeof(fs), "%sshaders/present.fs", ad);
    LS.presentShader = LoadShader(0, fs);
    LS.presentInited = LS.presentShader.id > 0;
}

// ── Sync helpers (delegate to AppState) ─────────────────────────────
void LayerStack_SyncRTFromImage(int idx) {
    if (!LS.app) return;
    SyncRTFromImage(LS.app, idx);
}
void LayerStack_SyncImageFromRT(int idx) {
    if (!LS.app) return;
    SyncImageFromRT(LS.app, idx);
}
void LayerStack_SyncLayerTex(int idx) {
    if (!LS.app) return;
    SyncLayerTexture(LS.app, idx);
}
void LayerStack_SyncAllRTs(void) {
    if (!LS.app) return;
    SyncAllRTs(LS.app);
}
void LayerStack_SyncAllImages(void) {
    if (!LS.app) return;
    SyncAllImages(LS.app);
}

// ── Layer management (delegate to Canvas API) ────────────────────────
int LayerStack_Add(void) {
    if (!LS.app) return -1;
    AppState* state = LS.app;
    Canvas_AddLayer(&state->canvas);
    int n = state->canvas.layerCount;
    int newIdx = n - 1;

    // Force realloc check by temporarily mismatching texCount
    state->texCount = newIdx; // one less than layerCount → EnsureRTs will extend
    EnsureRTs(state);         // allocates and clears the new slot, inits RT

    SyncRTFromImage(state, newIdx);
    LS.dirty = true;
    return newIdx;
}
void LayerStack_Delete(int idx) {
    if (!LS.app) return;
    AppState* state = LS.app;
    int n = state->canvas.layerCount;
    if (n <= 1 || idx < 0 || idx >= n) return;

    if (state->layerRTs[idx].id > 0)       UnloadRenderTexture(state->layerRTs[idx]);
    if (state->layerTextures[idx].id > 0)  UnloadTexture(state->layerTextures[idx]);

    Canvas_DeleteLayer(&state->canvas, idx);
    int newN = state->canvas.layerCount; // n-1

    for (int i = idx; i < newN; i++) {
        state->layerRTs[i]      = state->layerRTs[i + 1];
        state->layerTextures[i] = state->layerTextures[i + 1];
    }
    state->layerRTs[newN]      = RenderTexture2D{0};
    state->layerTextures[newN] = Texture2D{0};
    state->texCount = newN;
    LS.dirty = true;
}

void LayerStack_Duplicate(int idx) {
    if (!LS.app) return;
    Canvas_DuplicateLayer(&LS.app->canvas, idx);
    EnsureRTs(LS.app);
    SyncRTFromImage(LS.app, idx + 1);
    LS.dirty = true;
}
void LayerStack_Move(int from, int to) {
    if (!LS.app) return;
    Canvas_MoveLayer(&LS.app->canvas, from, to);
    SyncAllRTs(LS.app);
    LS.dirty = true;
}

void LayerStack_ApplyTransform(int idx, const float mat[6]) {
    if (!LS.app || idx < 0 || idx >= LS.app->canvas.layerCount) return;
    Layer_ApplyTransform(&LS.app->canvas.layerProps[idx], mat);
    LS.dirty = true;
}

// ── Compute relative matrix: inv(below) * top ────────────────────────
static void MatInvMul(const float below[6], const float top[6], float out[6]) {
    float a = below[0], b = below[1], tbx = below[2];
    float c = below[3], d = below[4], tby = below[5];
    float det = a * d - b * c;
    if (fabsf(det) < 0.0001f) { memcpy(out, top, 6 * sizeof(float)); return; }
    float id = 1.0f / det;
    float ia = d * id, ib = -b * id, itx = (b * tby - d * tbx) * id;
    float ic = -c * id, id_ = a * id, ity = (c * tbx - a * tby) * id;
    float ta = top[0], tb = top[1], ttx = top[2];
    float tc = top[3], td = top[4], tty = top[5];
    out[0] = ia * ta + ib * tc;
    out[1] = ia * tb + ib * td;
    out[2] = ia * ttx + ib * tty + itx;
    out[3] = ic * ta + id_ * tc;
    out[4] = ic * tb + id_ * td;
    out[5] = ic * ttx + id_ * tty + ity;
}
static void BakeTransformLooped(RenderTexture2D dst, Texture2D src, const float mat[6], int w, int h)
{
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(dst);
    ClearBackground(BLANK);
    float m[16] = {
        mat[0], mat[3], 0, 0,
        mat[1], mat[4], 0, 0,
        0,      0,      1, 0,
        mat[2], mat[5], 0, 1
    };
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            rlPushMatrix();
            float offset[16] = {
                1,0,0,0, 0,1,0,0, 0,0,1,0,
                (float)(dx*w), (float)(dy*h), 0, 1
            };
            rlMultMatrixf(offset);
            rlMultMatrixf(m);
            DrawTextureRec(src, Rectangle{0,0,(float)w,(float)-h}, Vector2{0,0}, WHITE);
            rlPopMatrix();
        }
    }
    EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
}

// ── Merge down ───────────────────────────────────────────────────────
void LayerStack_MergeDown(int idx) {
    if (!LS.app || !LS.shaderInited) return;
    AppState* state = LS.app;
    int layerCount = state->canvas.layerCount;
    RenderTexture2D* rts = state->layerRTs;
    Texture2D* texs = state->layerTextures;
    Image* imgs = state->canvas.layerImages;
    sLayerProps* props = state->canvas.layerProps;
    if (idx <= 0 || idx >= layerCount) return;
    if (rts[idx].id == 0 || rts[idx - 1].id == 0) return;

    float relMat[6];
    MatInvMul(props[idx - 1].mat, props[idx].mat, relMat);

    bool needBake = (relMat[0] != 1.0f || relMat[1] != 0.0f || relMat[2] != 0.0f ||
                     relMat[3] != 0.0f || relMat[4] != 1.0f || relMat[5] != 0.0f);
    Texture2D topTex = rts[idx].texture;
    if (needBake && LS.layerTransRT.id > 0) {
        rlSetBlendMode(RL_BLEND_CUSTOM);
        rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
        BeginTextureMode(LS.layerTransRT);
        ClearBackground(BLANK);
        rlPushMatrix();
        float m[16] = {
            relMat[0], relMat[3], 0, 0,
            relMat[1], relMat[4], 0, 0,
            0, 0, 1, 0,
            relMat[2], relMat[5], 0, 1
        };
        rlMultMatrixf(m);
        DrawTextureRec(rts[idx].texture,
            Rectangle{0, 0, (float)LS.cw, (float)-LS.ch}, Vector2{0, 0}, WHITE);
        rlPopMatrix();
        EndTextureMode();
        topTex = LS.layerTransRT.texture;
    }

    RenderTexture2D mergedRT = Load16BitRT(LS.cw, LS.ch);
    float alpha = props[idx].op;
    int bmidx = props[idx].blendmode;
    float threshold = props[idx].threshold;
    float feather = props[idx].feather;
    BeginTextureMode(mergedRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    if (alpha < 0.0001f && topTex.id == rts[idx].texture.id) {
        DrawTextureRec(rts[idx - 1].texture,
            Rectangle{0, 0, (float)LS.cw, (float)-LS.ch}, Vector2{0, 0}, WHITE);
    } else {
        BeginShaderMode(LS.blendShader);
        SetShaderValueTexture(LS.blendShader, LS.locLayerTex, topTex);
        SetShaderValue(LS.blendShader, LS.locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
        SetShaderValue(LS.blendShader, LS.locBmIdx, &bmidx, SHADER_UNIFORM_INT);
        SetShaderValue(LS.blendShader, LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
        SetShaderValue(LS.blendShader, LS.locLayerFeather, &feather, SHADER_UNIFORM_FLOAT);
        DrawTextureRec(rts[idx - 1].texture,
            Rectangle{0, 0, (float)LS.cw, (float)-LS.ch}, Vector2{0, 0}, WHITE);
        EndShaderMode();
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();

    // Swap merged RT into below layer, update image.
    // Do NOT touch texs — SyncLayerTexture manages that separately
    // and must not unload the RT's color attachment.
    RenderTexture2D oldRT = rts[idx - 1];
    rts[idx - 1] = mergedRT;
    Image cap = LoadImageFromTexture(mergedRT.texture);
    ImageFlipVertical(&cap);
    UnloadImage(imgs[idx - 1]);
    imgs[idx - 1] = cap;
    UnloadRenderTexture(oldRT);

    // Remove top layer (Canvas_DeleteLayer handles the image unload + array shift)
    if (rts[idx].id > 0) UnloadRenderTexture(rts[idx]);
    if (texs[idx].id > 0) UnloadTexture(texs[idx]);
    Canvas_DeleteLayer(&state->canvas, idx);
    for (int i = idx; i < state->canvas.layerCount; i++) {
        rts[i]   = rts[i + 1];
        texs[i]  = texs[i + 1];
    }
    int n = state->canvas.layerCount;
    rts[n] = RenderTexture2D{0};
    texs[n] = Texture2D{0};
    state->texCount = n;

    LS.dirty = true;
}

void LayerStack_MergeDownSeamless(int idx) {
    if (!LS.app || !LS.shaderInited) return;
    AppState* state = LS.app;
    int layerCount = state->canvas.layerCount;
    RenderTexture2D* rts = state->layerRTs;
    Texture2D* texs = state->layerTextures;
    Image* imgs = state->canvas.layerImages;
    sLayerProps* props = state->canvas.layerProps;
    int cw = LS.cw, ch = LS.ch;
    if (idx <= 0 || idx >= layerCount) return;
    if (rts[idx].id == 0 || rts[idx - 1].id == 0) return;

    float relMat[6];
    MatInvMul(props[idx - 1].mat, props[idx].mat, relMat);

    bool needBake = (relMat[0] != 1.0f || relMat[1] != 0.0f || relMat[2] != 0.0f ||
                     relMat[3] != 0.0f || relMat[4] != 1.0f || relMat[5] != 0.0f);
    Texture2D topTex = rts[idx].texture;
    if (needBake && LS.layerTransRT.id > 0) {
        BakeTransformLooped(LS.layerTransRT, rts[idx].texture, props[idx].mat, cw, ch);
        topTex = LS.layerTransRT.texture;
    }

    SetTextureWrap(topTex, TEXTURE_WRAP_REPEAT);

    RenderTexture2D mergedRT = Load16BitRT(cw, ch);
    float alpha = props[idx].op;
    int bmidx = props[idx].blendmode;
    float threshold = props[idx].threshold;
    float feather = props[idx].feather;

    BeginTextureMode(mergedRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(LS.blendShader);
    SetShaderValueTexture(LS.blendShader, LS.locLayerTex, topTex);
    SetShaderValue(LS.blendShader, LS.locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader, LS.locBmIdx, &bmidx, SHADER_UNIFORM_INT);
    SetShaderValue(LS.blendShader, LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader, LS.locLayerFeather, &feather, SHADER_UNIFORM_FLOAT);
    DrawTextureRec(rts[idx - 1].texture,
        Rectangle{0, 0, (float)cw, (float)-ch}, Vector2{0, 0}, WHITE);
    EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();

    // Swap merged RT into below layer
    RenderTexture2D oldRT = rts[idx - 1];
    rts[idx - 1] = mergedRT;

    // Capture image from merged RT
    Image cap = LoadImageFromTexture(mergedRT.texture);
    ImageFlipVertical(&cap);
    UnloadImage(imgs[idx - 1]);
    imgs[idx - 1] = cap;

    // Unload old RT (not texs — texs[idx-1] is a separate CPU-side texture, unload it separately)
    UnloadRenderTexture(oldRT);
    if (texs[idx - 1].id > 0) UnloadTexture(texs[idx - 1]);
    texs[idx - 1] = Texture2D{0};  // will be rebuilt on next SyncLayerTexture call

    // Remove top layer
    if (rts[idx].id > 0) UnloadRenderTexture(rts[idx]);
    if (texs[idx].id > 0) UnloadTexture(texs[idx]);
    Canvas_DeleteLayer(&state->canvas, idx);
    int n = state->canvas.layerCount;
    for (int i = idx; i < n; i++) {
        rts[i]  = rts[i + 1];
        texs[i] = texs[i + 1];
    }
    rts[n]  = RenderTexture2D{0};
    texs[n] = Texture2D{0};
    state->texCount = n;
    LS.dirty = true;
}

bool  LayerStack_PresentInited(void) { return LS.presentInited; }
Shader LayerStack_GetPresentShader(void) { return LS.presentShader; }
void LayerStack_SetDirty(void) { LS.dirty = true; }

// ── Compositing ─────────────────────────────────────────────────────
RenderTexture2D* LayerStack_Composite(void) {
    int w = LS.cw, h = LS.ch;
    if (w < 1 || h < 1) return NULL;

    EnsureAccumulators(w, h);
    EnsureChecker(w, h);
    EnsureShader();
    EnsurePresentShader();

    if (!LS.app) return NULL;
    int layerCount = LS.app->canvas.layerCount;
    RenderTexture2D* rts = LS.app->layerRTs;
    sLayerProps* props = LS.app->canvas.layerProps;

    if (LS.dirty || layersDirty) {
        LS.dirty = false;
        layersDirty = false;
        RenderTexture2D* src = &LS.accumA;
        RenderTexture2D* dst = &LS.accumB;

        BeginTextureMode(*src);
        ClearBackground(BLANK);
        DrawTexture(LS.checkerTex, 0, 0, WHITE);
        EndTextureMode();

        for (int i = 0; i < layerCount; i++) {
            if (!props[i].visible) continue;
            if (rts[i].id == 0) continue;

            float alpha = props[i].op;
            int bmidx = props[i].blendmode;
            float threshold = props[i].threshold;
            float feather = props[i].feather;

            bool hasTransform = (props[i].mat[0] != 1.0f || props[i].mat[1] != 0.0f ||
                                 props[i].mat[2] != 0.0f || props[i].mat[3] != 0.0f ||
                                 props[i].mat[4] != 1.0f || props[i].mat[5] != 0.0f);

            if (hasTransform && LS.layerTransRT.id > 0) {
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                BeginTextureMode(LS.layerTransRT);
                ClearBackground(BLANK);
                rlPushMatrix();
                float m[16] = {
                    props[i].mat[0], props[i].mat[3], 0, 0,
                    props[i].mat[1], props[i].mat[4], 0, 0,
                    0, 0, 1, 0,
                    props[i].mat[2], props[i].mat[5], 0, 1
                };
                rlMultMatrixf(m);
                DrawTextureRec(rts[i].texture,
                    Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, WHITE);
                rlPopMatrix();
                EndTextureMode();
            }

            BeginTextureMode(*dst);
            ClearBackground(BLANK);

            Texture2D layerTex = hasTransform ? LS.layerTransRT.texture : rts[i].texture;

            if (LS.shaderInited) {
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                BeginShaderMode(LS.blendShader);
                SetShaderValueTexture(LS.blendShader, LS.locLayerTex, layerTex);
                SetShaderValue(LS.blendShader, LS.locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
                SetShaderValue(LS.blendShader, LS.locBmIdx, &bmidx, SHADER_UNIFORM_INT);
                SetShaderValue(LS.blendShader, LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
                SetShaderValue(LS.blendShader, LS.locLayerFeather, &feather, SHADER_UNIFORM_FLOAT);
                DrawTextureRec(src->texture,
                    Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, WHITE);
                EndShaderMode();
                rlSetBlendMode(RL_BLEND_ALPHA);
            } else {
                DrawTextureRec(src->texture,
                    Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, WHITE);
                DrawTextureRec(layerTex,
                    Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, ColorAlpha(WHITE, alpha));
            }
            EndTextureMode();

            RenderTexture2D* tmp = src; src = dst; dst = tmp;
        }

        LS.finalAcc = src;
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
    return (LS.accumInited && LS.finalAcc) ? LS.finalAcc : NULL;
}

// ── Export helpers ────────────────────────────────────────────────────
Image LayerStack_CompositeWithDither(void) {
    EnsureShader();
    EnsurePresentShader();
    int w = LS.cw, h = LS.ch;
    if (w < 1 || h < 1 || !LS.app) return (Image){0};
    int layerCount = LS.app->canvas.layerCount;
    RenderTexture2D* rts = LS.app->layerRTs;
    sLayerProps* props = LS.app->canvas.layerProps;

    RenderTexture2D a = Load16BitRT(w, h);
    RenderTexture2D b = Load16BitRT(w, h);
    RenderTexture2D* src = &a;
    RenderTexture2D* dst = &b;

    BeginTextureMode(*src);
    ClearBackground(BLANK);
    for (int i = 0; i < layerCount; i++) {
        if (!props[i].visible) continue;
        if (rts[i].id == 0) continue;

        float alpha = props[i].op;
        int bmidx = props[i].blendmode;
        float threshold = props[i].threshold;
        float feather = props[i].feather;

        BeginTextureMode(*dst);
        ClearBackground(BLANK);
        if (LS.shaderInited) {
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
            BeginShaderMode(LS.blendShader);
            SetShaderValueTexture(LS.blendShader, LS.locLayerTex, rts[i].texture);
            SetShaderValue(LS.blendShader, LS.locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
            SetShaderValue(LS.blendShader, LS.locBmIdx, &bmidx, SHADER_UNIFORM_INT);
            SetShaderValue(LS.blendShader, LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
            SetShaderValue(LS.blendShader, LS.locLayerFeather, &feather, SHADER_UNIFORM_FLOAT);
            DrawTextureRec(src->texture, Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, WHITE);
            EndShaderMode();
        }
        EndTextureMode();
        RenderTexture2D* tmp = src; src = dst; dst = tmp;
    }

    BeginTextureMode(*dst);
    ClearBackground(BLANK);
    if (LS.presentInited) BeginShaderMode(LS.presentShader);
    DrawTextureRec(src->texture, Rectangle{0, 0, (float)w, (float)-h}, Vector2{0, 0}, WHITE);
    if (LS.presentInited) EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
    Image result = LoadImageFromTexture(dst->texture);
    ImageFlipVertical(&result);
    ImageFormat(&result, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a);
    UnloadRenderTexture(b);
    return result;
}
