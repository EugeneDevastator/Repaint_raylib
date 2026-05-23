#include "layerstack.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
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

static Rectangle FullRect(int w, int h) { return Rectangle{0, 0, (float)w, (float)-h}; }

// ── Init / shutdown / bind ───────────────────────────────────────────
void LayerStack_Init(void) {
    LS = {0};
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

void LayerStack_ReloadShader(void) {
    if (LS.shaderInited) { UnloadShader(LS.blendShader); LS.shaderInited = false; LS.dirty = true; }
    const char* ad = GetApplicationDirectory();
    char fs[512]; snprintf(fs, sizeof(fs), "%sshaders/layer_blend.fs", ad);
    LS.blendShader = LoadShader(0, fs);
    if (LS.blendShader.id == 0) { TraceLog(LOG_ERROR, "layer_blend.fs failed"); return; }
    LS.locLayerTex   = GetShaderLocation(LS.blendShader, "layerTex");
    LS.locLayerAlpha = GetShaderLocation(LS.blendShader, "layerAlpha");
    LS.locBmIdx      = GetShaderLocation(LS.blendShader, "bmidx");
    LS.locLayerThreshold = GetShaderLocation(LS.blendShader, "layerThreshold");
    LS.locLayerFeather   = GetShaderLocation(LS.blendShader, "layerFeather");
    LS.shaderInited = true;
}

bool   LayerStack_PresentInited(void)    { return LS.presentInited; }
Shader LayerStack_GetPresentShader(void) { return LS.presentShader; }

// ── Internal helpers ─────────────────────────────────────────────────
static void EnsureAccumulators(int w, int h) {
    if (LS.accumInited && LS.curCanvasW == w && LS.curCanvasH == h) return;
    if (LS.accumInited) { UnloadRenderTexture(LS.accumA); UnloadRenderTexture(LS.accumB); UnloadRenderTexture(LS.layerTransRT); }
    LS.accumA = Load16BitRT(w, h); LS.accumB = Load16BitRT(w, h);
    LS.layerTransRT = Load16BitRT(w, h);
    LS.curCanvasW = w; LS.curCanvasH = h;
    LS.accumInited = true; LS.finalAcc = NULL; LS.dirty = true;
}

static void EnsureChecker(int w, int h) {
    if (LS.checkerValid && LS.checkerTex.width == w && LS.checkerTex.height == h) return;
    if (LS.checkerTex.id > 0) UnloadTexture(LS.checkerTex);
    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y += 8)
        for (int x = 0; x < w; x += 8) {
            bool light = ((x / 8) + (y / 8)) % 2 == 0;
            Color col = light ? Color{70,70,75,255} : Color{55,55,60,255};
            ImageDrawRectangle(&img, x, y, 8, 8, col);
        }
    LS.checkerTex = LoadTextureFromImage(img); UnloadImage(img);
    LS.checkerValid = true;
}

static void EnsureShader(void) {
    if (LS.shaderInited) return;
    const char* ad = GetApplicationDirectory(); char fs[512];
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
    const char* ad = GetApplicationDirectory(); char fs[512];
    snprintf(fs, sizeof(fs), "%sshaders/present.fs", ad);
    LS.presentShader = LoadShader(0, fs);
    LS.presentInited = LS.presentShader.id > 0;
}

// ── Reallocate GPU arrays when layer count changes ──────────────────
static void GrowArrays(int newCount) {
    AppState* s = LS.app;
    s->layerRTs  = (RenderTexture2D*)realloc(s->layerRTs,  newCount * sizeof(RenderTexture2D));
    s->layerTextures = (Texture2D*)realloc(s->layerTextures, newCount * sizeof(Texture2D));
    s->texDirty  = (bool*)realloc(s->texDirty,  newCount * sizeof(bool));
    if (newCount > s->texCount) {
        memset(&s->layerRTs[s->texCount],  0, (newCount - s->texCount) * sizeof(RenderTexture2D));
        memset(&s->layerTextures[s->texCount], 0, (newCount - s->texCount) * sizeof(Texture2D));
    }
    s->texCount = newCount;
}

static void GrowCanvasData(int newCount) {
    Canvas* c = &LS.app->canvas;
    c->layerImages = (Image*)realloc(c->layerImages, newCount * sizeof(Image));
    c->layerProps  = (sLayerProps*)realloc(c->layerProps,  newCount * sizeof(sLayerProps));
    c->layerCount  = newCount;
}

static void SlotShift(int from, int to, int dir) {
    AppState* s = LS.app;
    Canvas* c = &s->canvas;
    if (dir > 0) { // shift left (remove slot at `from`)
        for (int i = from; i < to; i++) {
            s->layerRTs[i]   = s->layerRTs[i+1];
            s->layerTextures[i] = s->layerTextures[i+1];
            c->layerImages[i] = c->layerImages[i+1];
            c->layerProps[i]  = c->layerProps[i+1];
        }
    } else { // shift right (insert slot at `from`)
        for (int i = to; i > from; i--) {
            s->layerRTs[i]   = s->layerRTs[i-1];
            s->layerTextures[i] = s->layerTextures[i-1];
            c->layerImages[i] = c->layerImages[i-1];
            c->layerProps[i]  = c->layerProps[i-1];
        }
    }
}

// ── Layer management ─────────────────────────────────────────────────
int LayerStack_AddNew(int w, int h) {
    if (!LS.app) return -1;
    AppState* s = LS.app;
    int idx = s->canvas.layerCount;

    GrowCanvasData(idx + 1);
    GrowArrays(idx + 1);

    // Create image and RT at native resolution
    s->canvas.layerImages[idx] = GenImageColor(w, h, BLANK);
    ImageFormat(&s->canvas.layerImages[idx], PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    s->layerRTs[idx] = Load16BitRT(w, h);
    BeginTextureMode(s->layerRTs[idx]); ClearBackground(BLANK); EndTextureMode();
    s->layerTextures[idx] = LoadTextureFromImage(s->canvas.layerImages[idx]);

    // Default props
    sLayerProps* p = &s->canvas.layerProps[idx];
    memset(p, 0, sizeof(*p));
    p->op = 1.0f; p->visible = true; p->blendmode = bmGamma;
    p->threshold = 0.0f; p->feather = 1.0f;
    p->mat[0] = 1; p->mat[4] = 1;
    p->layerW = w; p->layerH = h;
    p->layerName[0] = '\0';

    LS.dirty = true;
    return idx;
}

int LayerStack_InsertLayer(int afterIdx) {
    if (!LS.app) return -1;
    int idx = afterIdx < 0 ? 0 : afterIdx > LS.app->canvas.layerCount ? LS.app->canvas.layerCount : afterIdx;
    int n = LS.app->canvas.layerCount;
    GrowCanvasData(n + 1);
    GrowArrays(n + 1);
    if (idx < n) SlotShift(idx, n, -1); // shift right

    int cw = CW(), ch = CH();
    LS.app->canvas.layerImages[idx] = GenImageColor(cw, ch, BLANK);
    ImageFormat(&LS.app->canvas.layerImages[idx], PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    LS.app->layerRTs[idx] = Load16BitRT(cw, ch);
    BeginTextureMode(LS.app->layerRTs[idx]); ClearBackground(BLANK); EndTextureMode();
    LS.app->layerTextures[idx] = LoadTextureFromImage(LS.app->canvas.layerImages[idx]);

    sLayerProps* p = &LS.app->canvas.layerProps[idx];
    memset(p, 0, sizeof(*p));
    p->op = 1.0f; p->visible = true; p->blendmode = bmGamma;
    p->threshold = 0.0f; p->feather = 1.0f;
    p->mat[0] = 1; p->mat[4] = 1;
    p->layerW = cw; p->layerH = ch;
    p->layerName[0] = '\0';

    LS.dirty = true;
    return idx;
}

void LayerStack_DeleteLayer(int idx) {
    if (!LS.app) return;
    int n = LS.app->canvas.layerCount;
    if (n <= 1 || idx < 0 || idx >= n) return;
    // Free GPU+CPU resources
    if (LS.app->layerRTs[idx].id > 0)  UnloadRenderTexture(LS.app->layerRTs[idx]);
    if (LS.app->layerTextures[idx].id > 0) UnloadTexture(LS.app->layerTextures[idx]);
    UnloadImage(LS.app->canvas.layerImages[idx]);
    // Shift arrays
    if (idx < n - 1) SlotShift(idx, n - 1, 1); // shift left
    LS.app->canvas.layerCount--;
    LS.app->texCount = LS.app->canvas.layerCount;
    LS.dirty = true;
}

void LayerStack_DuplicateLayer(int idx) {
    if (!LS.app || idx < 0 || idx >= LS.app->canvas.layerCount) return;
    int n = LS.app->canvas.layerCount;
    int newIdx = idx + 1;
    GrowCanvasData(n + 1);
    GrowArrays(n + 1);
    if (newIdx < n) SlotShift(newIdx, n, -1); // shift right

    LS.app->canvas.layerImages[newIdx] = ImageCopy(LS.app->canvas.layerImages[idx]);
    LS.app->canvas.layerProps[newIdx]  = LS.app->canvas.layerProps[idx];
    // Copy image from original RT to new RT at original resolution
    int w = LS.app->canvas.layerProps[idx].layerW;
    int h = LS.app->canvas.layerProps[idx].layerH;
    LS.app->layerRTs[newIdx] = Load16BitRT(w, h);
    BeginTextureMode(LS.app->layerRTs[newIdx]);
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTextureRec(LS.app->layerRTs[idx].texture, FullRect(w, h), Vector2{0,0}, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    LS.app->layerTextures[newIdx] = LoadTextureFromImage(LS.app->canvas.layerImages[newIdx]);

    LS.dirty = true;
}

void LayerStack_MoveLayer(int from, int to) {
    if (!LS.app) return;
    int n = LS.app->canvas.layerCount;
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    // Save the moved element
    RenderTexture2D movedRT   = LS.app->layerRTs[from];
    Texture2D       movedTex  = LS.app->layerTextures[from];
    Image           movedImg  = LS.app->canvas.layerImages[from];
    sLayerProps     movedProp = LS.app->canvas.layerProps[from];
    if (from < to) {
        for (int i = from; i < to; i++) {
            LS.app->layerRTs[i]   = LS.app->layerRTs[i+1];
            LS.app->layerTextures[i] = LS.app->layerTextures[i+1];
            LS.app->canvas.layerImages[i] = LS.app->canvas.layerImages[i+1];
            LS.app->canvas.layerProps[i]  = LS.app->canvas.layerProps[i+1];
        }
    } else {
        for (int i = from; i > to; i--) {
            LS.app->layerRTs[i]   = LS.app->layerRTs[i-1];
            LS.app->layerTextures[i] = LS.app->layerTextures[i-1];
            LS.app->canvas.layerImages[i] = LS.app->canvas.layerImages[i-1];
            LS.app->canvas.layerProps[i]  = LS.app->canvas.layerProps[i-1];
        }
    }
    LS.app->layerRTs[to]   = movedRT;
    LS.app->layerTextures[to]  = movedTex;
    LS.app->canvas.layerImages[to] = movedImg;
    LS.app->canvas.layerProps[to]  = movedProp;
    LS.dirty = true;
}

void LayerStack_ApplyTransform(int idx, const float mat[6]) {
    if (!LS.app || idx < 0 || idx >= LS.app->canvas.layerCount) return;
    sLayerProps* lp = &LS.app->canvas.layerProps[idx];
    float a=mat[0],b=mat[1],tx=mat[2],c=mat[3],d=mat[4],ty=mat[5];
    float ca=lp->mat[0],cb=lp->mat[1],ctx=lp->mat[2];
    float cc=lp->mat[3],cd=lp->mat[4],cty=lp->mat[5];
    lp->mat[0]=a*ca+b*cc; lp->mat[1]=a*cb+b*cd; lp->mat[2]=a*ctx+b*cty+tx;
    lp->mat[3]=c*ca+d*cc; lp->mat[4]=c*cb+d*cd; lp->mat[5]=c*ctx+d*cty+ty;
    LS.dirty = true;
}

// ── Bake helpers ─────────────────────────────────────────────────────
// ── BakeTransform ─────────────────────────────────────────────────
// Render a layer texture (lw × lh) into a canvas-sized temp RT via an
// affine 2×3 matrix.  The matrix maps the layer's native-resolution
// rectangle to its final position on the canvas.
static void BakeTransform(RenderTexture2D dst, Texture2D src,
    const float mat[6], int lw, int lh, int cw, int ch) {
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(dst); ClearBackground(BLANK);
    rlPushMatrix();
    float m[16] = { mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2],mat[5],0,1 };
    rlMultMatrixf(m);
    DrawTextureRec(src, Rectangle{0,0,(float)lw,(float)-lh}, Vector2{0,0}, WHITE);
    rlPopMatrix(); EndTextureMode();
}

static void BakeTransformLooped(RenderTexture2D dst, Texture2D src,
    const float mat[6], int lw, int lh, int cw, int ch) {
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(dst); ClearBackground(BLANK);
    rlPushMatrix();
    float m[16] = { mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2],mat[5],0,1 };
    rlMultMatrixf(m);
    DrawTextureRec(src, Rectangle{0,0,(float)lw,(float)-lh}, Vector2{0,0}, WHITE);
    rlPopMatrix();
    // Tile at wrapped positions
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0) continue;
        float tx = dx * cw, ty = dy * ch;
        rlPushMatrix();
        float tm[16] = { mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2]+tx,mat[5]+ty,0,1 };
        rlMultMatrixf(tm);
        DrawTextureRec(src, Rectangle{0,0,(float)lw,(float)-lh}, Vector2{0,0}, WHITE);
        rlPopMatrix();
    }
    EndTextureMode();
}

static void ApplyBlendShader(RenderTexture2D dst, Texture2D base, Texture2D layerTex,
    float alpha, int bmidx, float threshold, float feather, int w, int h) {
    BeginTextureMode(dst);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(LS.blendShader);
    SetShaderValueTexture(LS.blendShader, LS.locLayerTex, layerTex);
    SetShaderValue(LS.blendShader, LS.locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader, LS.locBmIdx, &bmidx, SHADER_UNIFORM_INT);
    SetShaderValue(LS.blendShader, LS.locLayerThreshold, &threshold, SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader, LS.locLayerFeather, &feather, SHADER_UNIFORM_FLOAT);
    DrawTextureRec(base, FullRect(w, h), Vector2{0,0}, WHITE);
    EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
}

// ── Merge down ───────────────────────────────────────────────────────
static void MatInvMul(const float below[6], const float top[6], float out[6]) {
    float a=below[0],b=below[1],tbx=below[2],c=below[3],d=below[4],tby=below[5];
    float det = a*d - b*c;
    if (fabsf(det) < 0.0001f) { memcpy(out, top, 6*sizeof(float)); return; }
    float id=1/det, ia=d*id, ib=-b*id, itx=(b*tby - d*tbx)*id;
    float ic=-c*id, id_=a*id, ity=(c*tbx - a*tby)*id;
    float ta=top[0],tb=top[1],ttx=top[2],tc=top[3],td=top[4],tty=top[5];
    out[0]=ia*ta+ib*tc;  out[1]=ia*tb+ib*td;  out[2]=ia*ttx+ib*tty+itx;
    out[3]=ic*ta+id_*tc; out[4]=ic*tb+id_*td; out[5]=ic*ttx+id_*tty+ity;
}

static Texture2D GetTransformedTop(RenderTexture2D* rts, sLayerProps* props, int idx, bool looped) {
    float relMat[6]; MatInvMul(props[idx-1].mat, props[idx].mat, relMat);
    if (LS.layerTransRT.id == 0) return rts[idx].texture;
    int cw = CW(), ch = CH();
    int lw = props[idx].layerW, lh = props[idx].layerH;
    if (looped)
        BakeTransformLooped(LS.layerTransRT, rts[idx].texture, props[idx].mat, lw, lh, cw, ch);
    else
        BakeTransform(LS.layerTransRT, rts[idx].texture, relMat, lw, lh, cw, ch);
    return LS.layerTransRT.texture;
}

static void RemoveLayerSlot(AppState* state, int idx) {
    RenderTexture2D* rts = state->layerRTs; Texture2D* texs = state->layerTextures;
    if (rts[idx].id  > 0) UnloadRenderTexture(rts[idx]);
    if (texs[idx].id > 0) UnloadTexture(texs[idx]);
    UnloadImage(state->canvas.layerImages[idx]);
    int n = state->canvas.layerCount - 1;
    for (int i = idx; i < n; i++) {
        rts[i] = rts[i+1]; texs[i] = texs[i+1];
        state->canvas.layerImages[i] = state->canvas.layerImages[i+1];
        state->canvas.layerProps[i]  = state->canvas.layerProps[i+1];
    }
    state->canvas.layerCount = n;
    state->texCount = n;
}

static void FinalizeMerge(AppState* state, int idx, RenderTexture2D mergedRT) {
    RenderTexture2D* rts = state->layerRTs; Texture2D* texs = state->layerTextures;
    RenderTexture2D oldRT = rts[idx-1];
    rts[idx-1] = mergedRT;
    Texture2D oldTex = texs[idx-1];
    texs[idx-1] = mergedRT.texture;
    Image cap = LoadImageFromTexture(mergedRT.texture);
    ImageFlipVertical(&cap);
    UnloadImage(state->canvas.layerImages[idx-1]);
    state->canvas.layerImages[idx-1] = cap;
    UnloadRenderTexture(oldRT);
    if (oldTex.id > 0) UnloadTexture(oldTex);
    RemoveLayerSlot(state, idx);
    LS.dirty = true;
}

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
    int bw = props[idx-1].layerW, bh = props[idx-1].layerH;
    RenderTexture2D mergedRT = Load16BitRT(bw, bh);
    sLayerProps* p = &props[idx];
    ApplyBlendShader(mergedRT, rts[idx-1].texture, topTex, p->op, p->blendmode, p->threshold, p->feather, bw, bh);
    FinalizeMerge(state, idx, mergedRT);
}

void LayerStack_MergeDown(int idx)         { MergeDownImpl(idx, false); }
void LayerStack_MergeDownSeamless(int idx) { MergeDownImpl(idx, true);  }

// ── Compositing ─────────────────────────────────────────────────────
RenderTexture2D* LayerStack_Composite(void) {
    if (!LS.app) return NULL;
    int cw = CW(), ch = CH();
    if (cw < 1 || ch < 1) return NULL;
    EnsureAccumulators(cw, ch);
    EnsureChecker(cw, ch);
    EnsureShader();
    EnsurePresentShader();

    if (LS.dirty || layersDirty) {
        LS.dirty = false; layersDirty = false;
        RenderTexture2D* src = &LS.accumA;
        RenderTexture2D* dst = &LS.accumB;

        BeginTextureMode(*src); ClearBackground(BLANK);
        DrawTexture(LS.checkerTex, 0, 0, WHITE);
        EndTextureMode();

        int layerCount = LS.app->canvas.layerCount;
        RenderTexture2D* rts = LS.app->layerRTs;
        sLayerProps* props = LS.app->canvas.layerProps;

        for (int i = 0; i < layerCount; i++) {
            if (!props[i].visible || rts[i].id == 0) continue;

            Texture2D layerTex = rts[i].texture;
            if (LS.layerTransRT.id > 0) {
                BakeTransform(LS.layerTransRT, rts[i].texture, props[i].mat,
                    props[i].layerW, props[i].layerH, cw, ch);
                layerTex = LS.layerTransRT.texture;
            }

            sLayerProps* p = &props[i];
            if (LS.shaderInited) {
                ApplyBlendShader(*dst, src->texture, layerTex, p->op, p->blendmode, p->threshold, p->feather, cw, ch);
            } else {
                BeginTextureMode(*dst); ClearBackground(BLANK);
                DrawTextureRec(src->texture, FullRect(cw,ch), Vector2{0,0}, WHITE);
                DrawTextureRec(layerTex,     FullRect(cw,ch), Vector2{0,0}, ColorAlpha(WHITE, p->op));
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
    EnsureShader(); EnsurePresentShader();
    int cw = CW(), ch = CH();
    if (cw < 1 || ch < 1) return (Image){0};
    int layerCount = LS.app->canvas.layerCount;
    RenderTexture2D* rts = LS.app->layerRTs; sLayerProps* props = LS.app->canvas.layerProps;
    RenderTexture2D a = Load16BitRT(cw,ch), b = Load16BitRT(cw,ch);
    RenderTexture2D* src = &a; RenderTexture2D* dst = &b;
    BeginTextureMode(*src); ClearBackground(BLANK); EndTextureMode();

    for (int i = 0; i < layerCount; i++) {
        if (!props[i].visible || rts[i].id == 0) continue;
        Texture2D layerTex = rts[i].texture;
        if (LS.layerTransRT.id > 0) {
            BakeTransform(LS.layerTransRT, rts[i].texture, props[i].mat,
                props[i].layerW, props[i].layerH, cw, ch);
            layerTex = LS.layerTransRT.texture;
        }
        sLayerProps* p = &props[i];
        if (LS.shaderInited)
            ApplyBlendShader(*dst, src->texture, layerTex, p->op, p->blendmode, p->threshold, p->feather, cw, ch);
        RenderTexture2D* tmp = src; src = dst; dst = tmp;
    }

    BeginTextureMode(*dst); ClearBackground(BLANK);
    if (LS.presentInited) BeginShaderMode(LS.presentShader);
    DrawTextureRec(src->texture, FullRect(cw,ch), Vector2{0,0}, WHITE);
    if (LS.presentInited) EndShaderMode(); EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
    Image result = LoadImageFromTexture(dst->texture);
    ImageFlipVertical(&result);
    ImageFormat(&result, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b);
    return result;
}
