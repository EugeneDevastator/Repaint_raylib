#include "compositor.h"
#include "layerstack.h"
#include "render_utils.h"
#include "RaylibUtils.h"
#include "xform.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
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
} CS;

bool layersDirty = true;

static int CW(void) { return LayerStack_RenderW(); }
static int CH(void) { return LayerStack_RenderH(); }
static Rectangle FullRect(int w, int h) { return Rectangle{0,0,(float)w,(float)-h}; }

// ── Shader loading ────────────────────────────────────────────────────
static bool LoadBlendShader(void) {
    const char* ad=GetApplicationDirectory(); char fs[512];
    snprintf(fs,sizeof(fs),"%sshaders/layer_blend.fs",ad);
    CS.blendShader=LoadShaderWithIncludes(0,fs);
    if(CS.blendShader.id==0){ TraceLog(LOG_ERROR,"layer_blend.fs failed"); return false; }
    CS.locLayerTex=GetShaderLocation(CS.blendShader,"layerTex");
    CS.locLayerAlpha=GetShaderLocation(CS.blendShader,"layerAlpha");
    CS.locBmIdx=GetShaderLocation(CS.blendShader,"bmidx");
    CS.locLayerThreshold=GetShaderLocation(CS.blendShader,"layerThreshold");
    CS.locLayerFeather=GetShaderLocation(CS.blendShader,"layerFeather");
    CS.locUnderTex=GetShaderLocation(CS.blendShader,"underTex");
    if(CS.locUnderTex>=0){ int u=0; SetShaderValue(CS.blendShader,CS.locUnderTex,&u,SHADER_UNIFORM_INT); }
    CS.shaderInited=true; return true;
}

static void EnsureShader(void) {
    if(CS.shaderInited) return;
    LoadBlendShader();
}

static void EnsurePresentShader(void) {
    if(CS.presentInited) return;
    const char* ad=GetApplicationDirectory(); char fs[512];
    snprintf(fs,sizeof(fs),"%sshaders/present.fs",ad);
    CS.presentShader=LoadShaderWithIncludes(0,fs); CS.presentInited=CS.presentShader.id>0;
    if(CS.presentInited) {
        CS.locPresentTex=GetShaderLocation(CS.presentShader,"presentTex");
        if(CS.locPresentTex>=0){ int u=0; SetShaderValue(CS.presentShader,CS.locPresentTex,&u,SHADER_UNIFORM_INT); }
        CS.locTexSize=GetShaderLocation(CS.presentShader,"texSize");
        CS.locApplyDither=GetShaderLocation(CS.presentShader,"applyDither");
        if(CS.locApplyDither>=0){ int v=1; SetShaderValue(CS.presentShader,CS.locApplyDither,&v,SHADER_UNIFORM_INT); }
    }
}

// ── Accumulator / checker management ─────────────────────────────────
static void EnsureAccumulators(int w, int h) {
    if(CS.accumInited&&CS.curCanvasW==w&&CS.curCanvasH==h) return;
    if(CS.accumInited){ UnloadRenderTexture(CS.accumA); UnloadRenderTexture(CS.accumB); UnloadRenderTexture(CS.layerTransRT); }
    CS.accumA=Load16BitRT(w,h); CS.accumB=Load16BitRT(w,h); CS.layerTransRT=Load16BitRT(w,h);
    CS.curCanvasW=w; CS.curCanvasH=h; CS.accumInited=true; CS.finalAcc=NULL; CS.dirty=true;
}

static void EnsureChecker(int w, int h) {
    if(CS.checkerValid&&CS.checkerTex.width==w&&CS.checkerTex.height==h) return;
    if(CS.checkerTex.id>0)UnloadTexture(CS.checkerTex);
    Image img=GenImageColor(w,h,BLANK);
    for(int y=0;y<h;y+=8) for(int x=0;x<w;x+=8){
        bool light=((x/8)+(y/8))%2==0;
        Color col=light?Color{70,70,75,255}:Color{55,55,60,255};
        ImageDrawRectangle(&img,x,y,8,8,col);
    }
    CS.checkerTex=LoadTextureFromImage(img); UnloadImage(img); CS.checkerValid=true;
}

// ── Matrix ───────────────────────────────────────────────────────────
static Texture2D GetTransformedTop(int idx) {
    sLayerProps* belowProp = LayerStack_GetProps(idx-1);
    sLayerProps* topProp = LayerStack_GetProps(idx);
    if(!belowProp||!topProp) return LayerStack_GetRT(idx).texture;
    float relMat[6]; Xform_MulInv(relMat, topProp->xform.mat, belowProp->xform.mat);
    if(CS.layerTransRT.id==0) return LayerStack_GetRT(idx).texture;
    int cw=CW(),ch=CH(),lw=topProp->layerW,lh=topProp->layerH;
    (void)cw;(void)ch;
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginTextureMode(CS.layerTransRT); ClearBackground(BLANK);
    EnsurePresentShader();
    if(CS.presentInited) {
        BeginShaderMode(CS.presentShader);
        Compositor_SetPresentTexSize(lw, lh);
        Compositor_SetPresentDither(false);
    }
    bool flip = (relMat[0]*relMat[4] - relMat[1]*relMat[3]) < 0.0f;
    if (flip) { rlDisableBackfaceCulling(); rlDrawRenderBatchActive(); }
    rlPushMatrix();
    float m[16]={relMat[0],relMat[3],0,0, relMat[1],relMat[4],0,0, 0,0,1,0, relMat[2],relMat[5],0,1};
    rlMultMatrixf(m);
    DrawTextureRec(LayerStack_GetRT(idx).texture,Rectangle{0,0,(float)lw,(float)-lh},Vector2{0,0},WHITE);
    rlPopMatrix();
    if (flip) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
    if(CS.presentInited) EndShaderMode();
    EndTextureMode();
    return CS.layerTransRT.texture;
}

// ── Bake / blend helpers ────────────────────────────────────────────
static void BakeTransform(RenderTexture2D dst, Texture2D src, const float mat[6], int lw, int lh) {
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginTextureMode(dst); ClearBackground(BLANK);
    EnsurePresentShader();
    if(CS.presentInited) {
        BeginShaderMode(CS.presentShader);
        Compositor_SetPresentTexSize(lw, lh);
        Compositor_SetPresentDither(false);
    }
    bool flip = (mat[0]*mat[4] - mat[1]*mat[3]) < 0.0f;
    if (flip) { rlDisableBackfaceCulling(); rlDrawRenderBatchActive(); }
    rlPushMatrix();
    float m[16]={mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2],mat[5],0,1};
    rlMultMatrixf(m);
    DrawTextureRec(src,Rectangle{0,0,(float)lw,(float)-lh},Vector2{0,0},WHITE);
    rlPopMatrix();
    if (flip) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
    if(CS.presentInited) EndShaderMode();
    EndTextureMode();
}

static void ApplyBlend(RenderTexture2D dst, Texture2D base, Texture2D layerTex,
    float alpha, int bmidx, float threshold, float feather, int w, int h) {
    BeginTextureMode(dst); rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginShaderMode(CS.blendShader);
    SetShaderValueTexture(CS.blendShader,CS.locLayerTex,layerTex);
    SetShaderValue(CS.blendShader,CS.locLayerAlpha,&alpha,SHADER_UNIFORM_FLOAT);
    SetShaderValue(CS.blendShader,CS.locBmIdx,&bmidx,SHADER_UNIFORM_INT);
    SetShaderValue(CS.blendShader,CS.locLayerThreshold,&threshold,SHADER_UNIFORM_FLOAT);
    SetShaderValue(CS.blendShader,CS.locLayerFeather,&feather,SHADER_UNIFORM_FLOAT);
    DrawTextureRec(base,FullRect(w,h),Vector2{0,0},WHITE);
    EndShaderMode(); rlSetBlendMode(RL_BLEND_ALPHA); EndTextureMode();
}

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
            BakeTransform(transRT,srcTex,tileMat,lw,lh);
            ApplyBlend(scratch,accum.texture,transRT.texture,op,bmIdx,threshold,feather,outW,outH);
            RenderTexture2D t=accum; accum=scratch; scratch=t;
        }
    }
    SetTextureWrap(srcTex,TEXTURE_WRAP_CLAMP);
}

static void CopyRT(RenderTexture2D dst, RenderTexture2D src, int w, int h) {
    BeginTextureMode(dst);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground(BLANK);
    DrawTextureRec(src.texture,FullRect(w,h),Vector2{0,0},WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
}

// ── Ping-pong layer blend loop ──────────────────────────────────────
static RenderTexture2D* CompositeLayersInto(RenderTexture2D& a, RenderTexture2D& b, int cw, int ch, const float* viewMat) {
    RenderTexture2D*src=&a,*dst=&b;
    float cv[6];
    if(viewMat) memcpy(cv, viewMat, 6*sizeof(float));
    else { const float* lscv = LayerStack_GetCanvasView(); memcpy(cv, lscv, 6*sizeof(float)); }
    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        RenderTexture2D layerRT = LayerStack_GetRT(i);
        if(!p||!p->visible||layerRT.id==0) continue;
        Texture2D layerTex=layerRT.texture;
        float cmb[6];
        Xform_Mul(cmb, cv, p->xform.mat);
        if(CS.layerTransRT.id>0){
            BakeTransform(CS.layerTransRT,layerTex,cmb,p->layerW,p->layerH);
            layerTex=CS.layerTransRT.texture;
        }
        bool seamlessBlended=(p->seamless && CS.shaderInited && CS.layerTransRT.id>0);
        if(seamlessBlended) {
            int lw=p->layerW, lh=p->layerH;
            BlendSeamlessTiles(*src,*dst,LayerStack_GetRT(i).texture,cmb,lw,lh,cw,ch,
                p->op,p->blendmode,p->threshold,p->feather,CS.layerTransRT);
        } else if(CS.shaderInited) {
            ApplyBlend(*dst,src->texture,layerTex,p->op,p->blendmode,p->threshold,p->feather,cw,ch);
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

// ── Public API ────────────────────────────────────────────────────────

void Compositor_Init(void) {
    CS = {0};
}

void Compositor_Shutdown(void) {
    if(CS.accumInited){ UnloadRenderTexture(CS.accumA); UnloadRenderTexture(CS.accumB); UnloadRenderTexture(CS.layerTransRT); CS.accumInited=false; }
    if(CS.checkerValid){ UnloadTexture(CS.checkerTex); CS.checkerValid=false; }
    if(CS.shaderInited){ UnloadShader(CS.blendShader); CS.shaderInited=false; }
    if(CS.presentInited){ UnloadShader(CS.presentShader); CS.presentInited=false; }
    CS={0};
}

void Compositor_ReloadShader(void) {
    if(CS.shaderInited){ UnloadShader(CS.blendShader); CS.shaderInited=false; CS.dirty=true; }
    LoadBlendShader();
}

RenderTexture2D* Compositor_Composite(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return NULL;
    EnsureAccumulators(cw,ch); EnsureChecker(cw,ch); EnsureShader(); EnsurePresentShader();
    if(!(CS.dirty||layersDirty)){ rlSetBlendMode(RL_BLEND_ALPHA); return (CS.accumInited&&CS.finalAcc)?CS.finalAcc:NULL; }
    CS.dirty=false; layersDirty=false;
    BeginTextureMode(CS.accumA); ClearBackground(BLANK); DrawTexture(CS.checkerTex,0,0,WHITE); EndTextureMode();
    CS.finalAcc=CompositeLayersInto(CS.accumA,CS.accumB,cw,ch);
    rlSetBlendMode(RL_BLEND_ALPHA);
    return (CS.accumInited&&CS.finalAcc)?CS.finalAcc:NULL;
}

Image Compositor_CompositeWithDither(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return (Image){0};
    EnsureShader(); EnsurePresentShader();
    RenderTexture2D a=Load16BitRT(cw,ch),b=Load16BitRT(cw,ch);
    BeginTextureMode(a); ClearBackground(BLANK); EndTextureMode();
    RenderTexture2D* finalAcc=CompositeLayersInto(a,b,cw,ch);
    RenderTexture2D*out=(finalAcc==&a) ? &b : &a;
    BeginTextureMode(*out); ClearBackground(BLANK);
    if(CS.presentInited)BeginShaderMode(CS.presentShader);
    if(CS.locTexSize>=0){ float ts[2]={(float)cw,(float)ch}; SetShaderValue(CS.presentShader,CS.locTexSize,ts,SHADER_UNIFORM_VEC2); }
    Compositor_SetPresentDither(true);
    DrawTextureRec(finalAcc->texture,FullRect(cw,ch),Vector2{0,0},WHITE);
    if(CS.presentInited)EndShaderMode(); EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    Image result=LoadImageFromTexture(out->texture); ImageFlipVertical(&result);
    ImageFormat(&result,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b); return result;
}

void Compositor_CompositeViewInto(RenderTexture2D dst, const float viewMat[6], int w, int h) {
    if(w<1||h<1||dst.id==0) return;
    EnsureChecker(CW(),CH()); EnsureShader();
    RenderTexture2D tmp=Load16BitRT(w,h);
    BeginTextureMode(dst); ClearBackground(BLANK);
    if(CS.checkerTex.id>0 && CW()>0 && CH()>0){
        rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
        float vm[6];
        if(viewMat) memcpy(vm, viewMat, 6*sizeof(float));
        else { const float* lscv = LayerStack_GetCanvasView(); memcpy(vm, lscv, 6*sizeof(float)); }
        float m[16]={vm[0],vm[3],0,0, vm[1],vm[4],0,0, 0,0,1,0, vm[2],vm[5],0,1};
        rlPushMatrix(); rlMultMatrixf(m);
        DrawTextureRec(CS.checkerTex, FullRect(CW(),CH()), Vector2{0,0}, WHITE);
        rlPopMatrix(); rlSetBlendMode(RL_BLEND_ALPHA);
    }
    EndTextureMode();
    bool ownsTrans=false;
    RenderTexture2D savedTransRT=CS.layerTransRT;
    if(CS.layerTransRT.id==0 || CS.layerTransRT.texture.width<(unsigned)w || CS.layerTransRT.texture.height<(unsigned)h){
        CS.layerTransRT=Load16BitRT(w,h); ownsTrans=true;
    }
    RenderTexture2D* finalAcc=CompositeLayersInto(dst, tmp, w, h, viewMat);
    if(finalAcc && finalAcc!=&dst){
        BeginTextureMode(dst); ClearBackground(BLANK);
        DrawTextureRec(finalAcc->texture,FullRect(w,h),Vector2{0,0},WHITE);
        EndTextureMode();
    }
    if(ownsTrans){ UnloadRenderTexture(CS.layerTransRT); CS.layerTransRT=savedTransRT; }
    UnloadRenderTexture(tmp);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

RenderTexture2D Compositor_MergeBlend(int topIdx, int bottomIdx, bool seamless) {
    sLayerProps* bottomProp = LayerStack_GetProps(bottomIdx);
    sLayerProps* topProp = LayerStack_GetProps(topIdx);
    RenderTexture2D bottomRT = LayerStack_GetRT(bottomIdx);
    RenderTexture2D topRT = LayerStack_GetRT(topIdx);
    EnsureShader();
    if(!CS.shaderInited||!bottomProp||!topProp||bottomRT.id==0||topRT.id==0)
        return (RenderTexture2D){0};
    int cw=CW(),ch=CH(),bw=bottomProp->layerW,bh=bottomProp->layerH;
    int lw=topProp->layerW,lh=topProp->layerH;

    if(!seamless) {
        Texture2D topTex=GetTransformedTop(topIdx);
        RenderTexture2D mergedRT=Load16BitRT(bw,bh);
        if(mergedRT.id==0) return (RenderTexture2D){0};
        ApplyBlend(mergedRT,bottomRT.texture,topTex,
            topProp->op,topProp->blendmode,topProp->threshold,topProp->feather,bw,bh);
        return mergedRT;
    }

    // Seamless
    float relMat[6]; Xform_MulInv(relMat, topProp->xform.mat, bottomProp->xform.mat);
    RenderTexture2D bufA=Load16BitRT(bw,bh), bufB=Load16BitRT(bw,bh);
    if(bufA.id==0||bufB.id==0){
        if(bufA.id>0)UnloadRenderTexture(bufA); if(bufB.id>0)UnloadRenderTexture(bufB);
        return (RenderTexture2D){0};
    }
    CopyRT(bufA,bottomRT,bw,bh);
    BlendSeamlessTiles(bufA,bufB,topRT.texture,relMat,lw,lh,cw,ch,
        topProp->op,topProp->blendmode,topProp->threshold,topProp->feather,CS.layerTransRT);
    UnloadRenderTexture(bufB);
    return bufA;
}

bool Compositor_PresentInited(void) { return CS.presentInited; }
Shader Compositor_GetPresentShader(void) { return CS.presentShader; }
void Compositor_SetPresentTexSize(int w, int h) {
    if(CS.locTexSize>=0){ float ts[2]={(float)w,(float)h}; SetShaderValue(CS.presentShader,CS.locTexSize,ts,SHADER_UNIFORM_VEC2); }
}
void Compositor_SetPresentDither(bool on) {
    if(CS.locApplyDither>=0){ int v=on?1:0; SetShaderValue(CS.presentShader,CS.locApplyDither,&v,SHADER_UNIFORM_INT); }
}
Texture2D Compositor_GetCheckerTex(void) {
    int cw=CW(),ch=CH();
    if(cw>0&&ch>0) EnsureChecker(cw,ch);
    return CS.checkerTex;
}
void Compositor_SetDirty(void) { CS.dirty=true; }
