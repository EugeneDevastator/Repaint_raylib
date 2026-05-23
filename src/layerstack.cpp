#include "layerstack.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

static struct {
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

static int CW(void) { return LS.app ? LS.app->canvas.width  : 0; }
static int CH(void) { return LS.app ? LS.app->canvas.height : 0; }

// ── RT helpers ───────────────────────────────────────────────────────
RenderTexture2D Load16BitRT(int w, int h) {
    RenderTexture2D target = {0};
    target.id = rlLoadFramebuffer();
    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        target.texture.id      = rlLoadTexture(NULL, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
        target.texture.width   = w; target.texture.height = h;
        target.texture.format  = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        target.texture.mipmaps = 1;
        target.depth.id        = rlLoadTextureDepth(w, h, true);
        target.depth.width     = w; target.depth.height = h;
        target.depth.format    = 19; target.depth.mipmaps = 1;
        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id,   RL_ATTACHMENT_DEPTH,          RL_ATTACHMENT_RENDERBUFFER, 0);
        rlFramebufferComplete(target.id);
        rlDisableFramebuffer();
    }
    return target;
}

// ── Draw helpers ─────────────────────────────────────────────────────
static Rectangle FullRect(int w, int h) { return Rectangle{0, 0, (float)w, (float)-h}; }

static void SetBlendCopy(void) {
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
}

static void BlitCopy(RenderTexture2D dst, Texture2D src, int w, int h) {
    BeginTextureMode(dst);
    SetBlendCopy();
    ClearBackground(BLANK);
    DrawTextureRec(src, FullRect(w, h), Vector2{0,0}, WHITE);
    EndTextureMode();
}

static void ApplyBlendShader(RenderTexture2D dst, Texture2D base, Texture2D layerTex,
                              float alpha, int bmidx, float threshold, float feather, int w, int h) {
    BeginTextureMode(dst);
    SetBlendCopy();
    ClearBackground(BLANK);
    BeginShaderMode(LS.blendShader);
    SetShaderValueTexture(LS.blendShader, LS.locLayerTex,       layerTex);
    SetShaderValue(LS.blendShader,        LS.locLayerAlpha,     &alpha,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader,        LS.locBmIdx,          &bmidx,     SHADER_UNIFORM_INT);
    SetShaderValue(LS.blendShader,        LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader,        LS.locLayerFeather,   &feather,   SHADER_UNIFORM_FLOAT);
    DrawTextureRec(base, FullRect(w, h), Vector2{0,0}, WHITE);
    EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
}

static void BuildMatrix16(const float mat[6], float m[16]) {
    m[0]=mat[0]; m[1]=mat[3]; m[2]=0; m[3]=0;
    m[4]=mat[1]; m[5]=mat[4]; m[6]=0; m[7]=0;
    m[8]=0;      m[9]=0;      m[10]=1; m[11]=0;
    m[12]=mat[2];m[13]=mat[5];m[14]=0; m[15]=1;
}

static void BakeTransform(RenderTexture2D dst, Texture2D src, const float mat[6], int w, int h) {
    float m[16]; BuildMatrix16(mat, m);
    BeginTextureMode(dst);
    SetBlendCopy();
    ClearBackground(BLANK);
    rlPushMatrix(); rlMultMatrixf(m);
    DrawTextureRec(src, FullRect(w, h), Vector2{0,0}, WHITE);
    rlPopMatrix();
    EndTextureMode();
}

// ── Init / shutdown ──────────────────────────────────────────────────
void LayerStack_Init(void) {
    LS.app = NULL;
    LS.accumInited = LS.checkerValid = LS.shaderInited = LS.presentInited = false;
    LS.finalAcc = NULL;
    LS.dirty = true;
    LS.curCanvasW = LS.curCanvasH = 0;
}

void LayerStack_Bind(AppState* state) {
    LS.app = state;
    LS.dirty = true;
}

void LayerStack_Shutdown(void) {
    if (LS.accumInited) {
        UnloadRenderTexture(LS.accumA);
        UnloadRenderTexture(LS.accumB);
        UnloadRenderTexture(LS.layerTransRT);
        LS.accumInited = false;
    }
    if (LS.checkerValid)  { UnloadTexture(LS.checkerTex);   LS.checkerValid  = false; }
    if (LS.shaderInited)  { UnloadShader(LS.blendShader);   LS.shaderInited  = false; }
    if (LS.presentInited) { UnloadShader(LS.presentShader); LS.presentInited = false; }
    LS.app = NULL;
}

// ── Shader loading ───────────────────────────────────────────────────
static void LoadShaderFromAppDir(Shader* out, const char* name) {
    char fs[512];
    snprintf(fs, sizeof(fs), "%sshaders/%s", GetApplicationDirectory(), name);
    *out = LoadShader(0, fs);
}

static void LoadBlendShader(void) {
    LoadShaderFromAppDir(&LS.blendShader, "layer_blend.fs");
    if (LS.blendShader.id == 0) { TraceLog(LOG_ERROR, "layer_blend.fs failed"); return; }
    LS.locLayerTex       = GetShaderLocation(LS.blendShader, "layerTex");
    LS.locLayerAlpha     = GetShaderLocation(LS.blendShader, "layerAlpha");
    LS.locBmIdx          = GetShaderLocation(LS.blendShader, "bmidx");
    LS.locLayerThreshold = GetShaderLocation(LS.blendShader, "layerThreshold");
    LS.locLayerFeather   = GetShaderLocation(LS.blendShader, "layerFeather");
    LS.shaderInited = true;
}

void LayerStack_ReloadShader(void) {
    if (LS.shaderInited) { UnloadShader(LS.blendShader); LS.shaderInited = false; LS.dirty = true; }
    LoadBlendShader();
}

static void EnsureShader(void) { if (!LS.shaderInited) LoadBlendShader(); }

static void EnsurePresentShader(void) {
    if (LS.presentInited) return;
    LoadShaderFromAppDir(&LS.presentShader, "present.fs");
    LS.presentInited = LS.presentShader.id > 0;
}

// ── Ensure GPU resources ─────────────────────────────────────────────
static void EnsureAccumulators(int w, int h) {
    if (LS.accumInited && LS.curCanvasW == w && LS.curCanvasH == h) return;
    if (LS.accumInited) {
        UnloadRenderTexture(LS.accumA);
        UnloadRenderTexture(LS.accumB);
        UnloadRenderTexture(LS.layerTransRT);
    }
    LS.accumA       = Load16BitRT(w, h);
    LS.accumB       = Load16BitRT(w, h);
    LS.layerTransRT = Load16BitRT(w, h);
    SetTextureWrap(LS.accumA.texture,       TEXTURE_WRAP_REPEAT);
    SetTextureWrap(LS.accumB.texture,       TEXTURE_WRAP_REPEAT);
    SetTextureWrap(LS.layerTransRT.texture, TEXTURE_WRAP_REPEAT);
    LS.curCanvasW = w; LS.curCanvasH = h;
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
            bool light = ((x/8) + (y/8)) % 2 == 0;
            Color col = light ? Color{70,70,75,255} : Color{55,55,60,255};
            ImageDrawRectangle(&img, x, y, 8, 8, col);
        }
    LS.checkerTex = LoadTextureFromImage(img);
    UnloadImage(img);
    LS.checkerValid = true;
}

// ── Query ────────────────────────────────────────────────────────────
bool   LayerStack_PresentInited(void)    { return LS.presentInited; }
Shader LayerStack_GetPresentShader(void) { return LS.presentShader; }

// ── Layer slot removal ───────────────────────────────────────────────
static void RemoveLayerSlot(AppState* state, int idx) {
    RenderTexture2D* rts  = state->layerRTs;
    Texture2D*       texs = state->layerTextures;
    if (rts[idx].id  > 0) UnloadRenderTexture(rts[idx]);
    if (texs[idx].id > 0) UnloadTexture(texs[idx]);
    Canvas_DeleteLayer(&state->canvas, idx);
    int n = state->canvas.layerCount;
    for (int i = idx; i < n; i++) { rts[i] = rts[i+1]; texs[i] = texs[i+1]; }
    rts[n]  = RenderTexture2D{0};
    texs[n] = Texture2D{0};
    state->texCount = n;
}

// ── Layer management (encapsulates Canvas data + GPU sync) ──────────
int LayerStack_InsertLayer(int afterIdx) {
    if (!LS.app) return -1;
    AppState* state = LS.app;
    Canvas_InsertLayer(&state->canvas, afterIdx);
    int newIdx = afterIdx;
    EnsureRTs(state);
    SyncRTFromImage(state, newIdx);
    LS.dirty = true;
    return newIdx;
}

void LayerStack_DeleteLayer(int idx) {
    if (!LS.app) return;
    int n = LS.app->canvas.layerCount;
    if (n <= 1 || idx < 0 || idx >= n) return;
    RemoveLayerSlot(LS.app, idx);
    LS.dirty = true;
}

void LayerStack_DuplicateLayer(int idx) {
    if (!LS.app) return;
    AppState* state = LS.app;
    Canvas_DuplicateLayer(&state->canvas, idx);
    EnsureRTs(state);
    SyncRTFromImage(state, idx + 1);
    LS.dirty = true;
}

void LayerStack_MoveLayer(int from, int to) {
    if (!LS.app) return;
    AppState* state = LS.app;
    Canvas_MoveLayer(&state->canvas, from, to);
    SyncAllRTs(state);
    LS.dirty = true;
}

// ── Matrix helpers ───────────────────────────────────────────────────
static void MatInvMul(const float below[6], const float top[6], float out[6]) {
    float a=below[0], b=below[1], tbx=below[2];
    float c=below[3], d=below[4], tby=below[5];
    float det = a*d - b*c;
    if (fabsf(det) < 0.0001f) { memcpy(out, top, 6*sizeof(float)); return; }
    float id=1.0f/det;
    float ia=d*id, ib=-b*id, itx=(b*tby-d*tbx)*id;
    float ic=-c*id, id_=a*id, ity=(c*tbx-a*tby)*id;
    float ta=top[0], tb=top[1], ttx=top[2];
    float tc=top[3], td=top[4], tty=top[5];
    out[0]=ia*ta+ib*tc;  out[1]=ia*tb+ib*td;  out[2]=ia*ttx+ib*tty+itx;
    out[3]=ic*ta+id_*tc; out[4]=ic*tb+id_*td; out[5]=ic*ttx+id_*tty+ity;
}

static bool IsIdentityMat(const float mat[6]) {
    return mat[0]==1.0f && mat[1]==0.0f && mat[2]==0.0f &&
           mat[3]==0.0f && mat[4]==1.0f && mat[5]==0.0f;
}

// ── BakeTransformLooped ──────────────────────────────────────────────
static void BakeTransformLooped(RenderTexture2D dst, Texture2D src, const float mat[6], int w, int h) {
    float m[16]; BuildMatrix16(mat, m);
    RenderTexture2D pingA = Load16BitRT(w, h);
    RenderTexture2D pingB = Load16BitRT(w, h);
    RenderTexture2D* cur = &pingA;
    RenderTexture2D* nxt = &pingB;

    BeginTextureMode(*cur); SetBlendCopy(); ClearBackground(BLANK); EndTextureMode();

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            BeginTextureMode(*nxt);
            SetBlendCopy();
            ClearBackground(BLANK);
            DrawTextureRec(cur->texture, FullRect(w, h), Vector2{0,0}, WHITE);
            rlSetBlendFactors(RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            rlPushMatrix();
            float offset[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, (float)(dx*w),(float)(dy*h),0,1 };
            rlMultMatrixf(offset);
            rlMultMatrixf(m);
            DrawTextureRec(src, FullRect(w, h), Vector2{0,0}, WHITE);
            rlPopMatrix();
            EndTextureMode();
            RenderTexture2D* tmp = cur; cur = nxt; nxt = tmp;
        }
    }
    BlitCopy(dst, cur->texture, w, h);
    rlSetBlendMode(RL_BLEND_ALPHA);
    UnloadRenderTexture(pingA);
    UnloadRenderTexture(pingB);
}

// ── Shared merge core ────────────────────────────────────────────────
static void FinalizeMerge(AppState* state, int idx, RenderTexture2D mergedRT) {
    RenderTexture2D* rts  = state->layerRTs;
    Texture2D*       texs = state->layerTextures;
    Image*           imgs = state->canvas.layerImages;

    RenderTexture2D oldRT = rts[idx-1];
    rts[idx-1] = mergedRT;
    Image cap = LoadImageFromTexture(mergedRT.texture);
    ImageFlipVertical(&cap);
    UnloadImage(imgs[idx-1]);
    imgs[idx-1] = cap;
    UnloadRenderTexture(oldRT);
    if (texs[idx-1].id > 0) { UnloadTexture(texs[idx-1]); texs[idx-1] = Texture2D{0}; }

    RemoveLayerSlot(state, idx);
    LS.dirty = true;
}

static Texture2D GetTransformedTop(RenderTexture2D* rts, sLayerProps* props, int idx, bool looped) {
    float relMat[6];
    MatInvMul(props[idx-1].mat, props[idx].mat, relMat);
    if (IsIdentityMat(relMat) || LS.layerTransRT.id == 0)
        return rts[idx].texture;
    if (looped)
        BakeTransformLooped(LS.layerTransRT, rts[idx].texture, props[idx].mat, CW(), CH());
    else
        BakeTransform(LS.layerTransRT, rts[idx].texture, relMat, CW(), CH());
    return LS.layerTransRT.texture;
}

// ── Merge down ───────────────────────────────────────────────────────
static void MergeDownImpl(int idx, bool seamless) {
    if (!LS.app || !LS.shaderInited) return;
    AppState* state = LS.app;
    int layerCount = state->canvas.layerCount;
    if (idx <= 0 || idx >= layerCount) return;
    RenderTexture2D* rts = state->layerRTs;
    if (rts[idx].id == 0 || rts[idx-1].id == 0) return;

    sLayerProps* props = state->canvas.layerProps;
    Texture2D topTex = GetTransformedTop(rts, props, idx, seamless);
    if (seamless) SetTextureWrap(topTex, TEXTURE_WRAP_REPEAT);

    int cw = CW(), ch = CH();
    RenderTexture2D mergedRT = Load16BitRT(cw, ch);
    sLayerProps* p = &props[idx];
    ApplyBlendShader(mergedRT, rts[idx-1].texture, topTex, p->op, p->blendmode, p->threshold, p->feather, cw, ch);
    FinalizeMerge(state, idx, mergedRT);
}

void LayerStack_MergeDown(int idx)         { MergeDownImpl(idx, false); }
void LayerStack_MergeDownSeamless(int idx) { MergeDownImpl(idx, true);  }

// ── Compositing ──────────────────────────────────────────────────────
RenderTexture2D* LayerStack_Composite(void) {
    if (!LS.app) return NULL;
    int w = CW(), h = CH();
    if (w < 1 || h < 1) return NULL;
    EnsureAccumulators(w, h);
    EnsureChecker(w, h);
    EnsureShader();
    EnsurePresentShader();
    if (!LS.app) return NULL;

    int layerCount = LS.app->canvas.layerCount;
    RenderTexture2D* rts  = LS.app->layerRTs;
    sLayerProps*     props = LS.app->canvas.layerProps;

    if (LS.dirty || layersDirty) {
        LS.dirty = layersDirty = false;
        RenderTexture2D* src = &LS.accumA;
        RenderTexture2D* dst = &LS.accumB;

        BeginTextureMode(*src);
        ClearBackground(BLANK);
        DrawTexture(LS.checkerTex, 0, 0, WHITE);
        EndTextureMode();

        for (int i = 0; i < layerCount; i++) {
            if (!props[i].visible || rts[i].id == 0) continue;

            Texture2D layerTex = rts[i].texture;
            if (!IsIdentityMat(props[i].mat) && LS.layerTransRT.id > 0) {
                BakeTransform(LS.layerTransRT, rts[i].texture, props[i].mat, w, h);
                layerTex = LS.layerTransRT.texture;
            }

            sLayerProps* p = &props[i];
            if (LS.shaderInited) {
                ApplyBlendShader(*dst, src->texture, layerTex, p->op, p->blendmode, p->threshold, p->feather, w, h);
            } else {
                BeginTextureMode(*dst);
                ClearBackground(BLANK);
                DrawTextureRec(src->texture, FullRect(w, h), Vector2{0,0}, WHITE);
                DrawTextureRec(layerTex,     FullRect(w, h), Vector2{0,0}, ColorAlpha(WHITE, p->op));
                EndTextureMode();
            }
            RenderTexture2D* tmp = src; src = dst; dst = tmp;
        }
        LS.finalAcc = src;
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
    return (LS.accumInited && LS.finalAcc) ? LS.finalAcc : NULL;
}

// ── Export ───────────────────────────────────────────────────────────
Image LayerStack_CompositeWithDither(void) {
    if (!LS.app) return (Image){0};
    EnsureShader();
    EnsurePresentShader();
    int w = CW(), h = CH();
    if (w < 1 || h < 1) return (Image){0};

    int layerCount = LS.app->canvas.layerCount;
    RenderTexture2D* rts  = LS.app->layerRTs;
    sLayerProps*     props = LS.app->canvas.layerProps;

    RenderTexture2D a = Load16BitRT(w, h), b = Load16BitRT(w, h);
    RenderTexture2D* src = &a;
    RenderTexture2D* dst = &b;

    BeginTextureMode(*src); ClearBackground(BLANK); EndTextureMode();

    for (int i = 0; i < layerCount; i++) {
        if (!props[i].visible || rts[i].id == 0) continue;
        sLayerProps* p = &props[i];
        bool hasTransform = (p->mat[0] != 1.0f || p->mat[1] != 0.0f || p->mat[2] != 0.0f ||
                             p->mat[3] != 0.0f || p->mat[4] != 1.0f || p->mat[5] != 0.0f);
        Texture2D layerTex = rts[i].texture;
        if (hasTransform && LS.layerTransRT.id > 0) {
            BakeTransform(LS.layerTransRT, rts[i].texture, p->mat, w, h);
            layerTex = LS.layerTransRT.texture;
        }
        if (LS.shaderInited)
            ApplyBlendShader(*dst, src->texture, layerTex, p->op, p->blendmode, p->threshold, p->feather, w, h);
        RenderTexture2D* tmp = src; src = dst; dst = tmp;
    }

    BeginTextureMode(*dst);
    ClearBackground(BLANK);
    if (LS.presentInited) BeginShaderMode(LS.presentShader);
    DrawTextureRec(src->texture, FullRect(w, h), Vector2{0,0}, WHITE);
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
