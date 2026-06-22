#include "layerstack.h"
#include "RaylibUtils.h"
#include "undo.h"
#include "texture_manager.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// Create a 16-bit unsigned normalized render-target texture (GL_RGBA16).
// Raylib's PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 maps to GL_RGBA16F internally,
// so we bypass rlLoadTexture and use glTexImage2D directly.
static unsigned int CreateTexRGBA16(int w, int h) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT, NULL);
    return id;
}

// ── Internal state ────────────────────────────────────────────────────
static struct {
    sLayerProps* prop;
    RenderTexture2D* rt;
    TexSlotID* slotID;   // TM slot for this layer (registered via TM_Register)
    int count;
    int renderW, renderH;
    float canvasView[6]; // always pre-multiplied into every layer's transform
    RenderTexture2D accumA, accumB, layerTransRT;
    bool accumInited;
    Texture2D checkerTex; bool checkerValid;
    Shader blendShader; bool shaderInited;
    int locLayerTex, locLayerAlpha, locBmIdx, locLayerThreshold, locLayerFeather;
    int locUnderTex;
    Shader presentShader; bool presentInited;
    int locPresentTex;
    int locTexSize;
    int locApplyDither;
    int curCanvasW, curCanvasH;
    RenderTexture2D* finalAcc;
    bool dirty;
} LS;

static int CW(void) { return LS.renderW; }
static int CH(void) { return LS.renderH; }
static Rectangle FullRect(int w, int h) { return Rectangle{0,0,(float)w,(float)-h}; }

RenderTexture2D Load16BitRT(int w, int h) {
    RenderTexture2D t={0}; t.id=rlLoadFramebuffer();
    if(t.id>0){ rlEnableFramebuffer(t.id);
        t.texture.id=CreateTexRGBA16(w, h);
        t.texture.width=w; t.texture.height=h; t.texture.format=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16; t.texture.mipmaps=1;
        t.depth.id=rlLoadTextureDepth(w,h,true); t.depth.width=w; t.depth.height=h; t.depth.format=19; t.depth.mipmaps=1;
        rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
        rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_RENDERBUFFER,0);
        rlFramebufferComplete(t.id); rlDisableFramebuffer();
    } return t;
}

// ── Helper: load blend shader (extracted — used by ReloadShader + EnsureShader) ──
static bool LoadBlendShader(void) {
    const char* ad=GetApplicationDirectory(); char fs[512];
    snprintf(fs,sizeof(fs),"%sshaders/layer_blend.fs",ad);
    LS.blendShader=LoadShaderWithIncludes(0,fs);
    if(LS.blendShader.id==0){ TraceLog(LOG_ERROR,"layer_blend.fs failed"); return false; }
    LS.locLayerTex=GetShaderLocation(LS.blendShader,"layerTex");
    LS.locLayerAlpha=GetShaderLocation(LS.blendShader,"layerAlpha");
    LS.locBmIdx=GetShaderLocation(LS.blendShader,"bmidx");
    LS.locLayerThreshold=GetShaderLocation(LS.blendShader,"layerThreshold");
    LS.locLayerFeather=GetShaderLocation(LS.blendShader,"layerFeather");
    LS.locUnderTex=GetShaderLocation(LS.blendShader,"underTex");
    if(LS.locUnderTex>=0){ int u=0; SetShaderValue(LS.blendShader,LS.locUnderTex,&u,SHADER_UNIFORM_INT); }
    LS.shaderInited=true; return true;
}

// ── Helper: unload a single slot's GPU/CPU resources (no array shift) ──
static void UnloadLayerSlotResources(int idx) {
    // Unregister from TM if no other layer shares this slot
    if (TM_IsValid(LS.slotID[idx])) {
        bool shared = false;
        TexSlotID sid = LS.slotID[idx];
        for (int j = 0; j < LS.count; j++) {
            if (j != idx && LS.slotID[j].bucket == sid.bucket && LS.slotID[j].slot == sid.slot) {
                shared = true;
                break;
            }
        }
        if (!shared) {
            TM_Remove(LS.slotID[idx]);
        }
        LS.slotID[idx] = TM_INVALID_SLOT;
    }
    // Check if any other layer shares this RT — skip unloading if so
    bool shared = false;
    if(LS.rt[idx].id>0) {
        for(int j=0;j<LS.count;j++) {
            if(j!=idx && LS.rt[j].id==LS.rt[idx].id) { shared=true; break; }
        }
    }
    if(!shared) {
        if(LS.rt[idx].id>0) UnloadRenderTexture(LS.rt[idx]);
    }
}

// ── Helper: copy one RT's content into another with (ONE, ZERO, ADD) ──
static void CopyRT(RenderTexture2D dst, RenderTexture2D src, int w, int h) {
    BeginTextureMode(dst);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground(BLANK);
    DrawTextureRec(src.texture,FullRect(w,h),Vector2{0,0},WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
}

// ── Helper: shift slot array entries up (make room at `to`) ──
static void ShiftLayersUp(int from, int to) {
    for(int i=from;i>to;i--){
        LS.prop[i]=LS.prop[i-1];
        LS.rt[i]=LS.rt[i-1];
        LS.slotID[i]=LS.slotID[i-1];
    }
}

// ── Helper: shift slot array entries down (close gap at `from`) ──
static void ShiftLayersDown(int from, int to) {
    for(int i=from;i<to;i++){
        LS.prop[i]=LS.prop[i+1];
        LS.rt[i]=LS.rt[i+1];
        LS.slotID[i]=LS.slotID[i+1];
    }
}

// ── Helper: init a new slot with a blank image/RT at the given size ──
static void InitLayerSlot(int idx, int w, int h) {
    LS.rt[idx]=Load16BitRT(w,h);
    SetTextureFilter(LS.rt[idx].texture, TEXTURE_FILTER_BILINEAR);
    BeginTextureMode(LS.rt[idx]); ClearBackground(BLANK); EndTextureMode();
    LS.prop[idx]={}; LS.prop[idx].op=1; LS.prop[idx].visible=true;
    LS.prop[idx].blendmode=bmGamma; LS.prop[idx].xform = RectXform_Pivot(0, 0, (float)w, (float)h, 0);
    LS.prop[idx].threshold=0; LS.prop[idx].feather=1;
    LS.prop[idx].layerW=w; LS.prop[idx].layerH=h;
    char name[64]; snprintf(name, sizeof(name), "Layer %d", idx);
    LS.slotID[idx]=TM_Register(TM_BUCKET_LAYER, LS.rt[idx],
                                name, false, w, h);
}

// ── Helper: sync texture-manager slot RT + dims with current layer ──
static void SetSlotFromLayer(int idx) {
    TexSlot* ts = TM_Get(LS.slotID[idx]);
    if (ts) {
        ts->rt = LS.rt[idx];
        ts->w = LS.prop[idx].layerW;
        ts->h = LS.prop[idx].layerH;
    }
}

// ── Helper: remove a layer slot (unload resources + shift down) ──
static void RemoveLayerSlot(int idx) {
    UnloadLayerSlotResources(idx);
    ShiftLayersDown(idx,LS.count-1);
    LS.count--; LS.dirty=true;
}

// ── Init / shutdown ──────────────────────────────────────────────────
void LayerStack_Init(void) { LS = {0}; LS.canvasView[0]=1; LS.canvasView[4]=1; }

void LayerStack_Shutdown(void) {
    if(LS.accumInited){ UnloadRenderTexture(LS.accumA); UnloadRenderTexture(LS.accumB); UnloadRenderTexture(LS.layerTransRT); LS.accumInited=false; }
    if(LS.checkerValid){ UnloadTexture(LS.checkerTex); LS.checkerValid=false; }
    if(LS.shaderInited){ UnloadShader(LS.blendShader); LS.shaderInited=false; }
    if(LS.presentInited){ UnloadShader(LS.presentShader); LS.presentInited=false; }
    for(int i=0;i<LS.count;i++) UnloadLayerSlotResources(i);
    free(LS.prop); free(LS.rt); free(LS.slotID);
    LS={0};
}

void LayerStack_ReloadShader(void) {
    if(LS.shaderInited){ UnloadShader(LS.blendShader); LS.shaderInited=false; LS.dirty=true; }
    LoadBlendShader();
}

void LayerStack_SetRenderWindow(int w, int h) { LS.renderW=w; LS.renderH=h; LS.dirty=true; }

void LayerStack_SetCanvasView(const float mat[6]) {
    memcpy(LS.canvasView, mat, 6*sizeof(float));
    LS.dirty = true;
}

const float* LayerStack_GetCanvasView(void) {
    return LS.canvasView;
}

// ── Accessors ─────────────────────────────────────────────────────────
int LayerStack_Count(void) { return LS.count; }
sLayerProps*   LayerStack_GetProps(int i){ return (i>=0&&i<LS.count)?&LS.prop[i]:NULL; }

RenderTexture2D LayerStack_GetRT(int i)   { return (i>=0&&i<LS.count)?LS.rt[i]:RenderTexture2D{0}; }
TexSlotID       LayerStack_GetSlotID(int i){ return (i>=0&&i<LS.count)?LS.slotID[i]:TM_INVALID_SLOT; }
int             LayerStack_FindLayerBySlot(TexSlotID slot) {
    if (!TM_IsValid(slot)) return -1;
    for (int i = 0; i < LS.count; i++)
        if (LS.slotID[i].bucket == slot.bucket && LS.slotID[i].slot == slot.slot)
            return i;
    return -1;
}
int LayerStack_RenderW(void) { return LS.renderW; }
int LayerStack_RenderH(void) { return LS.renderH; }

// ── Internal array resize ────────────────────────────────────────────
static void ReallocArrays(int n) {
    LS.prop=(sLayerProps*)realloc(LS.prop,n*sizeof(sLayerProps));
    LS.rt=(RenderTexture2D*)realloc(LS.rt,n*sizeof(RenderTexture2D));
    LS.slotID=(TexSlotID*)realloc(LS.slotID,n*sizeof(TexSlotID));
    if(n>LS.count){
        memset(&LS.rt[LS.count],0,(n-LS.count)*sizeof(RenderTexture2D));
        for(int i=LS.count;i<n;i++) LS.slotID[i]=TM_INVALID_SLOT;
    }
}

// ── Layer management ─────────────────────────────────────────────────
int LayerStack_Add(int w, int h) {
    int idx=LS.count;
    ReallocArrays(idx+1);
    InitLayerSlot(idx,w,h);
    LS.count++; LS.dirty=true;
    // New layer starts with empty undo — don't touch existing layers' history
    return idx;
}

int LayerStack_InsertLayer(int afterIdx) {
    int idx=afterIdx<0?0:afterIdx>LS.count?LS.count:afterIdx;
    int cw=LS.renderW>0?LS.renderW:512, ch=LS.renderH>0?LS.renderH:512;
    ReallocArrays(LS.count+1);
    ShiftLayersUp(LS.count,idx);
    InitLayerSlot(idx,cw,ch);
    LS.count++; LS.dirty=true;
    return idx;
}

void LayerStack_DeleteLayer(int idx) {
    if(idx<0||idx>=LS.count||LS.count<=1) return;
    TexSlotID sid = LS.slotID[idx];
    RemoveLayerSlot(idx);
    if (g_undoManager && TM_IsValid(sid))
        g_undoManager->InvalidateSlot(sid);
}

void LayerStack_DuplicateLayer(int idx) {
    if(idx<0||idx>=LS.count) return;
    int n=LS.count; ReallocArrays(n+1);
    ShiftLayersUp(n,idx+1);
    int di=idx+1;
    LS.prop[di]=LS.prop[idx];
    int lw=LS.prop[idx].layerW, lh=LS.prop[idx].layerH;
    LS.rt[di]=Load16BitRT(lw,lh);
    CopyRT(LS.rt[di],LS.rt[idx],lw,lh);
    char name[64]; snprintf(name, sizeof(name), "Layer %d (dup)", di);
    LS.slotID[di]=TM_Register(TM_BUCKET_LAYER, LS.rt[di],
                               name, false, lw, lh);
    LS.count++; LS.dirty=true;
}

void LayerStack_DuplicateAsInstance(int idx) {
    if(idx<0||idx>=LS.count) return;
    int n=LS.count; ReallocArrays(n+1);
    ShiftLayersUp(n,idx+1);
    int di=idx+1;
    LS.prop[di]=LS.prop[idx];
    LS.prop[di].instanced=true;
    snprintf(LS.prop[di].layerName, sizeof(LS.prop[di].layerName),
             "%s(INST)", LS.prop[idx].layerName);
    // Share RT with the original
    LS.rt[di]=LS.rt[idx];
    // Share TM slot — increment refcount
    LS.slotID[di]=LS.slotID[idx];
    TM_AddRef(LS.slotID[idx]);
    LS.count++; LS.dirty=true;
}

void LayerStack_MoveLayer(int from, int to) {
    if(from<0||from>=LS.count||to<0||to>=LS.count||from==to) return;
    RenderTexture2D mvRT=LS.rt[from];
    sLayerProps mvProp=LS.prop[from];
    TexSlotID mvSlot=LS.slotID[from];
    if(from<to) ShiftLayersDown(from,to);
    else        ShiftLayersUp(from,to);
    LS.prop[to]=mvProp; LS.rt[to]=mvRT;
    LS.slotID[to]=mvSlot;
    LS.dirty=true;
}


// ── Sync ──────────────────────────────────────────────────────────────
Image LayerStack_ReadFromGPU(int idx) {
    Image cap = {0};
    if(idx>=0&&idx<LS.count&&LS.rt[idx].id>0){
        cap=LoadImageFromTexture(LS.rt[idx].texture); ImageFlipVertical(&cap);
        if(cap.format!=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16){
            int px=cap.width*cap.height; uint16_t*d=(uint16_t*)malloc(px*4*2); uint8_t*s=(uint8_t*)cap.data;
            for(int i=0;i<px;i++){ d[i*4]=s[i*4]*257; d[i*4+1]=s[i*4+1]*257; d[i*4+2]=s[i*4+2]*257; d[i*4+3]=s[i*4+3]*257; }
            free(cap.data); cap.data=d; cap.format=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        }
    }
    return cap;
}

void LayerStack_UploadToGPU(int idx, Image img) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0||!img.data) return;
    Texture2D tmp = LoadTextureFromImage(img);
    BeginTextureMode(LS.rt[idx]);
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);
    LS.dirty=true;
    UnloadImage(img);
}

// ── Bake / blend helpers ────────────────────────────────────────────
static void EnsurePresentShader(void);
static void BakeTransform(RenderTexture2D dst, Texture2D src, const float mat[6], int lw, int lh, int cw, int ch) {
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginTextureMode(dst); ClearBackground(BLANK);
    // Use perceptual bilinear shader for layer transform sampling
    EnsurePresentShader();
    if(LS.presentInited) {
        BeginShaderMode(LS.presentShader);
        LayerStack_SetPresentTexSize(lw, lh);
        LayerStack_SetPresentDither(false);
    }
    bool flip = (mat[0]*mat[4] - mat[1]*mat[3]) < 0.0f;
    if (flip) { rlDisableBackfaceCulling(); rlDrawRenderBatchActive(); }
    rlPushMatrix();
    float m[16]={mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2],mat[5],0,1};
    rlMultMatrixf(m);
    DrawTextureRec(src,Rectangle{0,0,(float)lw,(float)-lh},Vector2{0,0},WHITE);
    rlPopMatrix();
    if (flip) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
    if(LS.presentInited) EndShaderMode();
    EndTextureMode();
}

static void ApplyBlendShader(RenderTexture2D dst, Texture2D base, Texture2D layerTex,
    float alpha, int bmidx, float threshold, float feather, int w, int h) {
    BeginTextureMode(dst); rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginShaderMode(LS.blendShader);
    SetShaderValueTexture(LS.blendShader,LS.locLayerTex,layerTex);
    SetShaderValue(LS.blendShader,LS.locLayerAlpha,&alpha,SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader,LS.locBmIdx,&bmidx,SHADER_UNIFORM_INT);
    SetShaderValue(LS.blendShader,LS.locLayerThreshold,&threshold,SHADER_UNIFORM_FLOAT);
    SetShaderValue(LS.blendShader,LS.locLayerFeather,&feather,SHADER_UNIFORM_FLOAT);
    DrawTextureRec(base,FullRect(w,h),Vector2{0,0},WHITE);
    EndShaderMode(); rlSetBlendMode(RL_BLEND_ALPHA); EndTextureMode();
}

// ── Matrix ───────────────────────────────────────────────────────────
static void MatInvMul(const float below[6], const float top[6], float out[6]) {
    float a=below[0],b=below[1],tbx=below[2],c=below[3],d=below[4],tby=below[5];
    float det=a*d-b*c;
    if(fabsf(det)<0.0001f){ memcpy(out,top,6*sizeof(float)); return; }
    float id=1/det, ia=d*id, ib=-b*id, itx=(b*tby-d*tbx)*id;
    float ic=-c*id, id_=a*id, ity=(c*tbx-a*tby)*id;
    float ta=top[0],tb=top[1],ttx=top[2],tc=top[3],td=top[4],tty=top[5];
    out[0]=ia*ta+ib*tc; out[1]=ia*tb+ib*td; out[2]=ia*ttx+ib*tty+itx;
    out[3]=ic*ta+id_*tc; out[4]=ic*tb+id_*td; out[5]=ic*ttx+id_*tty+ity;
}

static Texture2D GetTransformedTop(int idx) {
    float relMat[6]; MatInvMul(LS.prop[idx-1].xform.mat, LS.prop[idx].xform.mat, relMat);
    if(LS.layerTransRT.id==0) return LS.rt[idx].texture;
    int cw=CW(),ch=CH(),lw=LS.prop[idx].layerW,lh=LS.prop[idx].layerH;
    BakeTransform(LS.layerTransRT,LS.rt[idx].texture,relMat,lw,lh,cw,ch);
    return LS.layerTransRT.texture;
}

// ── Shared seamless-tile blend ───────────────────────────────────────
// Renders 3×3 tiles of srcTex through `mat` into ping-pong accumulators.
// - `accum` on entry holds composite-so-far; on exit holds the final blend.
// - `scratch` is a temp buffer; contents undefined after return.
// - `mat` maps layer-local → output space (canvas or viewport).
// - Restores TEXTURE_WRAP_CLAMP on srcTex after the loop.
static void BlendSeamlessTiles(
    RenderTexture2D& accum, RenderTexture2D& scratch,
    Texture2D srcTex, const float mat[6], int lw, int lh,
    int outW, int outH,
    float op, int bmIdx, float threshold, float feather,
    RenderTexture2D transRT)
{
    SetTextureWrap(srcTex,TEXTURE_WRAP_REPEAT);
    for(int dy=-1;dy<=1;dy++){
        for(int dx=-1;dx<=1;dx++){
            float tileMat[6];
            memcpy(tileMat,mat,6*sizeof(float));
            tileMat[2]+=dx*(float)lw; tileMat[5]+=dy*(float)lh;
            BakeTransform(transRT,srcTex,tileMat,lw,lh,outW,outH);
            ApplyBlendShader(scratch,accum.texture,transRT.texture,op,bmIdx,threshold,feather,outW,outH);
            RenderTexture2D t=accum; accum=scratch; scratch=t;
        }
    }
    SetTextureWrap(srcTex,TEXTURE_WRAP_CLAMP);
}

// ── Merge down ────────────────────────────────────────────────────────
static bool IsRTShared(int idx) {
    if(!LS.rt[idx].id) return false;
    for(int j=0;j<LS.count;j++) if(j!=idx && LS.rt[j].id==LS.rt[idx].id) return true;
    return false;
}

static void MergeDownImpl(int idx, bool seamless) {
    if(!LS.shaderInited||idx<=0||idx>=LS.count||LS.rt[idx].id==0||LS.rt[idx-1].id==0) return;
    int cw=CW(),ch=CH(),bw=LS.prop[idx-1].layerW,bh=LS.prop[idx-1].layerH;
    int lw=LS.prop[idx].layerW,lh=LS.prop[idx].layerH;
    sLayerProps*p=&LS.prop[idx];

    if(!seamless) {
        Texture2D topTex=GetTransformedTop(idx);
        bool bottomShared = IsRTShared(idx-1);
        RenderTexture2D mergedRT=Load16BitRT(bw,bh);
        ApplyBlendShader(mergedRT,LS.rt[idx-1].texture,topTex,p->op,p->blendmode,p->threshold,p->feather,bw,bh);
        if(bottomShared) {
            CopyRT(LS.rt[idx-1], mergedRT, bw, bh);
            UnloadRenderTexture(mergedRT);
        } else {
            RenderTexture2D oldRT=LS.rt[idx-1]; LS.rt[idx-1]=mergedRT;
            UnloadRenderTexture(oldRT);
        }
        SetSlotFromLayer(idx-1);
        TexSlotID sidTop = LS.slotID[idx];
        TexSlotID sidBot = LS.slotID[idx-1];
        RemoveLayerSlot(idx);
        if (g_undoManager) {
            g_undoManager->InvalidateSlot(sidTop);
            g_undoManager->InvalidateSlot(sidBot);
        }
        return;
    }

    // ── Seamless: 3×3 tile blend via shared helper ────────────────────
    float relMat[6]; MatInvMul(LS.prop[idx-1].xform.mat, LS.prop[idx].xform.mat, relMat);
    RenderTexture2D bufA=Load16BitRT(bw,bh), bufB=Load16BitRT(bw,bh);
    if(bufA.id==0||bufB.id==0){ if(bufA.id>0)UnloadRenderTexture(bufA); if(bufB.id>0)UnloadRenderTexture(bufB); return; }

    // Seed bufA with the bottom layer
    CopyRT(bufA,LS.rt[idx-1],bw,bh);

    BlendSeamlessTiles(bufA,bufB,LS.rt[idx].texture,relMat,lw,lh,cw,ch,
        p->op,p->blendmode,p->threshold,p->feather,LS.layerTransRT);

    // bufA holds the final blended result
    RenderTexture2D mergedRT=bufA;
    UnloadRenderTexture(bufB);
    bool bottomShared = IsRTShared(idx-1);
    if(bottomShared) {
        CopyRT(LS.rt[idx-1], mergedRT, bw, bh);
        UnloadRenderTexture(mergedRT);
    } else {
        RenderTexture2D oldRT=LS.rt[idx-1]; LS.rt[idx-1]=mergedRT;
        UnloadRenderTexture(oldRT);
    }
    SetSlotFromLayer(idx-1);
    TexSlotID sidTop = LS.slotID[idx];
    TexSlotID sidBot = LS.slotID[idx-1];
    RemoveLayerSlot(idx);
    if (g_undoManager) {
        g_undoManager->InvalidateSlot(sidTop);
        g_undoManager->InvalidateSlot(sidBot);
    }
}

void LayerStack_MergeDown(int idx) { MergeDownImpl(idx,false); }
void LayerStack_MergeDownSeamless(int idx) { MergeDownImpl(idx,true); }

// ── Compositing ──────────────────────────────────────────────────────
static void EnsureAccumulators(int w, int h) {
    if(LS.accumInited&&LS.curCanvasW==w&&LS.curCanvasH==h) return;
    if(LS.accumInited){ UnloadRenderTexture(LS.accumA); UnloadRenderTexture(LS.accumB); UnloadRenderTexture(LS.layerTransRT); }
    LS.accumA=Load16BitRT(w,h); LS.accumB=Load16BitRT(w,h); LS.layerTransRT=Load16BitRT(w,h);
    LS.curCanvasW=w; LS.curCanvasH=h; LS.accumInited=true; LS.finalAcc=NULL; LS.dirty=true;
}
static void EnsureChecker(int w, int h) {
    if(LS.checkerValid&&LS.checkerTex.width==w&&LS.checkerTex.height==h) return;
    if(LS.checkerTex.id>0)UnloadTexture(LS.checkerTex);
    Image img=GenImageColor(w,h,BLANK);
    for(int y=0;y<h;y+=8) for(int x=0;x<w;x+=8){
        bool light=((x/8)+(y/8))%2==0;
        Color col=light?Color{70,70,75,255}:Color{55,55,60,255};
        ImageDrawRectangle(&img,x,y,8,8,col);
    }
    LS.checkerTex=LoadTextureFromImage(img); UnloadImage(img); LS.checkerValid=true;
}
static void EnsureShader(void) {
    if(LS.shaderInited) return;
    LoadBlendShader();
}
static void EnsurePresentShader(void) {
    if(LS.presentInited) return;
    const char* ad=GetApplicationDirectory(); char fs[512];
    snprintf(fs,sizeof(fs),"%sshaders/present.fs",ad);
    LS.presentShader=LoadShaderWithIncludes(0,fs); LS.presentInited=LS.presentShader.id>0;
    if(LS.presentInited) {
        LS.locPresentTex=GetShaderLocation(LS.presentShader,"presentTex");
        if(LS.locPresentTex>=0){ int u=0; SetShaderValue(LS.presentShader,LS.locPresentTex,&u,SHADER_UNIFORM_INT); }
        LS.locTexSize=GetShaderLocation(LS.presentShader,"texSize");
        LS.locApplyDither=GetShaderLocation(LS.presentShader,"applyDither");
        if(LS.locApplyDither>=0){ int v=1; SetShaderValue(LS.presentShader,LS.locApplyDither,&v,SHADER_UNIFORM_INT); }
    }
}

// ── Helper: run the ping-pong layer blend loop ──
// Returns pointer to whichever RT holds the final accumulated result.
// When viewMat is non-NULL it replaces LS.canvasView (used by CompositeViewInto).
static RenderTexture2D* CompositeLayersInto(RenderTexture2D& a, RenderTexture2D& b, int cw, int ch, const float* viewMat) {
    RenderTexture2D*src=&a,*dst=&b;
    float cv[6];
    if(viewMat) memcpy(cv, viewMat, 6*sizeof(float));
    else        memcpy(cv, LS.canvasView, 6*sizeof(float));
    for(int i=0;i<LS.count;i++){
        if(!LS.prop[i].visible||LS.rt[i].id==0) continue;
        Texture2D layerTex=LS.rt[i].texture;
        sLayerProps*p=&LS.prop[i];
        float cmb[6];
        Xform_Mul(cmb, cv, p->xform.mat);
        if(LS.layerTransRT.id>0){
            BakeTransform(LS.layerTransRT,LS.rt[i].texture,cmb,LS.prop[i].layerW,LS.prop[i].layerH,cw,ch);
            layerTex=LS.layerTransRT.texture;
        }
        bool seamlessBlended=(p->seamless && LS.shaderInited && LS.layerTransRT.id>0);
        if(seamlessBlended) {
            int lw=p->layerW, lh=p->layerH;
            BlendSeamlessTiles(*src,*dst,LS.rt[i].texture,cmb,lw,lh,cw,ch,
                p->op,p->blendmode,p->threshold,p->feather,LS.layerTransRT);
        } else if(LS.shaderInited) {
            ApplyBlendShader(*dst,src->texture,layerTex,p->op,p->blendmode,p->threshold,p->feather,cw,ch);
            RenderTexture2D*tmp=src; src=dst; dst=tmp;
        } else {
            BeginTextureMode(*dst); ClearBackground(BLANK);
            DrawTextureRec(src->texture,FullRect(cw,ch),Vector2{0,0},WHITE);
            DrawTextureRec(layerTex,FullRect(cw,ch),Vector2{0,0},ColorAlpha(WHITE,p->op));
            EndTextureMode();
            RenderTexture2D*tmp=src; src=dst; dst=tmp;
        }
    }
    return src;
}

static RenderTexture2D* CompositeLayersInto(RenderTexture2D& a, RenderTexture2D& b, int cw, int ch) {
    return CompositeLayersInto(a, b, cw, ch, NULL);
}

bool LayerStack_PresentInited(void) { return LS.presentInited; }
Shader LayerStack_GetPresentShader(void) { return LS.presentShader; }
void LayerStack_SetPresentTexSize(int w, int h) {
    if(LS.locTexSize>=0){ float ts[2]={(float)w,(float)h}; SetShaderValue(LS.presentShader,LS.locTexSize,ts,SHADER_UNIFORM_VEC2); }
}
void LayerStack_SetPresentDither(bool on) {
    if(LS.locApplyDither>=0){ int v=on?1:0; SetShaderValue(LS.presentShader,LS.locApplyDither,&v,SHADER_UNIFORM_INT); }
}
Texture2D LayerStack_GetCheckerTex(void) {
    // Ensure the checker is created at the current render-window size
    int cw=CW(),ch=CH();
    if(cw>0&&ch>0) EnsureChecker(cw,ch);
    return LS.checkerTex;
}
void LayerStack_SetDirty(void) { LS.dirty=true; }

RenderTexture2D* LayerStack_Composite(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return NULL;
    EnsureAccumulators(cw,ch); EnsureChecker(cw,ch); EnsureShader(); EnsurePresentShader();
    if(!(LS.dirty||layersDirty)){ rlSetBlendMode(RL_BLEND_ALPHA); return (LS.accumInited&&LS.finalAcc)?LS.finalAcc:NULL; }
    LS.dirty=false; layersDirty=false;
    BeginTextureMode(LS.accumA); ClearBackground(BLANK); DrawTexture(LS.checkerTex,0,0,WHITE); EndTextureMode();
    LS.finalAcc=CompositeLayersInto(LS.accumA,LS.accumB,cw,ch);
    rlSetBlendMode(RL_BLEND_ALPHA);
    return (LS.accumInited&&LS.finalAcc)?LS.finalAcc:NULL;
}

Image LayerStack_CompositeWithDither(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return (Image){0};
    EnsureShader(); EnsurePresentShader();
    RenderTexture2D a=Load16BitRT(cw,ch),b=Load16BitRT(cw,ch);
    BeginTextureMode(a); ClearBackground(BLANK); EndTextureMode();
    RenderTexture2D* finalAcc=CompositeLayersInto(a,b,cw,ch);
    // Write the other buffer so we never read and write the same RT
    RenderTexture2D*out=(finalAcc==&a) ? &b : &a;
    BeginTextureMode(*out); ClearBackground(BLANK);
    if(LS.presentInited)BeginShaderMode(LS.presentShader);
    if(LS.locTexSize>=0){ float ts[2]={(float)cw,(float)ch}; SetShaderValue(LS.presentShader,LS.locTexSize,ts,SHADER_UNIFORM_VEC2); }
    LayerStack_SetPresentDither(true);
    DrawTextureRec(finalAcc->texture,FullRect(cw,ch),Vector2{0,0},WHITE);
    if(LS.presentInited)EndShaderMode(); EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    Image result=LoadImageFromTexture(out->texture); ImageFlipVertical(&result);
    ImageFormat(&result,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b); return result;
}

// ── View-matrix compositing (bypasses canvasView, used by crop mode) ──
void LayerStack_CompositeViewInto(RenderTexture2D dst, const float viewMat[6], int w, int h) {
    if(w<1||h<1||dst.id==0) return;
    EnsureChecker(CW(),CH()); EnsureShader();
    RenderTexture2D tmp=Load16BitRT(w,h);
    // Seed with checkerboard at canvas position transformed by viewMat
    BeginTextureMode(dst); ClearBackground(BLANK);
    if(LS.checkerTex.id>0 && CW()>0 && CH()>0){
        rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
        float vm[6]; memcpy(vm, viewMat ? viewMat : LS.canvasView, sizeof(vm));
        float m[16]={vm[0],vm[3],0,0, vm[1],vm[4],0,0, 0,0,1,0, vm[2],vm[5],0,1};
        rlPushMatrix(); rlMultMatrixf(m);
        DrawTextureRec(LS.checkerTex, FullRect(CW(),CH()), Vector2{0,0}, WHITE);
        rlPopMatrix(); rlSetBlendMode(RL_BLEND_ALPHA);
    }
    EndTextureMode();
    // Use viewport-sized transRT so layers beyond the canvas aren't clipped
    bool ownsTrans=false;
    RenderTexture2D savedTransRT=LS.layerTransRT;
    if(LS.layerTransRT.id==0 || LS.layerTransRT.texture.width<(unsigned)w || LS.layerTransRT.texture.height<(unsigned)h){
        LS.layerTransRT=Load16BitRT(w,h); ownsTrans=true;
    }
    CompositeLayersInto(dst, tmp, w, h, viewMat);
    if(ownsTrans){ UnloadRenderTexture(LS.layerTransRT); LS.layerTransRT=savedTransRT; }
    UnloadRenderTexture(tmp);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

// ── New compositing API (caller-owned targets, arbitrary resolution) ──

void LayerStack_BakeSingleLayer(int idx, RenderTexture2D dst) {
    if (idx < 0 || idx >= LS.count) return;
    if (LS.rt[idx].id == 0 || dst.id == 0) return;
    EnsurePresentShader();
    BakeTransform(dst, LS.rt[idx].texture, LS.prop[idx].xform.mat,
                  LS.prop[idx].layerW, LS.prop[idx].layerH, 0, 0);
}

bool LayerStack_GetSceneBounds(Rectangle* out) {
    if (!out) return false;
    bool any = false;
    Rectangle aabb = {0};
    for (int i = 0; i < LS.count; i++) {
        if (!LS.prop[i].visible || LS.rt[i].id == 0) continue;
        Rectangle layerBB = GetWorldAABB(&LS.prop[i].xform);
        if (!any) { aabb = layerBB; any = true; }
        else {
            float l = fminf(aabb.x, layerBB.x);
            float t = fminf(aabb.y, layerBB.y);
            float r = fmaxf(aabb.x + aabb.width, layerBB.x + layerBB.width);
            float b = fmaxf(aabb.y + aabb.height, layerBB.y + layerBB.height);
            aabb.x = l; aabb.y = t; aabb.width = r-l; aabb.height = b-t;
        }
    }
    *out = aabb;
    return any;
}

void LayerStack_BakeCanvasWindow(const Document* doc) {
    (void)doc;
    // Non-destructive: bake canvasView into each layer's transform only.
    // Layer image content is untouched.
    for (int i = 0; i < LS.count; i++) {
        if (LS.rt[i].id == 0) continue;
        sLayerProps* p = &LS.prop[i];
        float cmb[6];
        Xform_Mul(cmb, LS.canvasView, p->xform.mat);
        memcpy(p->xform.mat, cmb, 6*sizeof(float));
    }
    Xform_Identity(LS.canvasView);
    LS.dirty = true;
}
