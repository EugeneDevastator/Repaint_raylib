#include "compositor.h"
#include "render_utils.h"
#include "RaylibUtils.h"
#include "xform.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    RenderTexture2D layerTransRT;
    bool transInited;
    int transW, transH;
    Texture2D checkerTex; bool checkerValid;
    int checkerW, checkerH;
    Shader blendShader; bool shaderInited;
    int locLayerTex, locLayerAlpha, locBmIdx, locLayerThreshold, locLayerFeather;
    int locUnderTex;
    Shader presentShader; bool presentInited;
    int locPresentTex;
    int locTexSize;
    int locApplyDither;
} CS;

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

// ── Checker ───────────────────────────────────────────────────────────
static void EnsureChecker(int w, int h) {
    if(CS.checkerValid&&CS.checkerW==w&&CS.checkerH==h) return;
    if(CS.checkerTex.id>0)UnloadTexture(CS.checkerTex);
    Image img=GenImageColor(w,h,BLANK);
    for(int y=0;y<h;y+=8) for(int x=0;x<w;x+=8){
        bool light=((x/8)+(y/8))%2==0;
        Color col=light?Color{70,70,75,255}:Color{55,55,60,255};
        ImageDrawRectangle(&img,x,y,8,8,col);
    }
    CS.checkerTex=LoadTextureFromImage(img); UnloadImage(img);
    CS.checkerW=w; CS.checkerH=h; CS.checkerValid=true;
}

// ── Temp rectification RT ────────────────────────────────────────────
static RenderTexture2D GetTransRT(int needW, int needH) {
    if(CS.transInited&&CS.transW>=needW&&CS.transH>=needH)
        return CS.layerTransRT;
    if(CS.transInited){ UnloadRenderTexture(CS.layerTransRT); CS.transInited=false; }
    CS.layerTransRT=Load16BitRT(needW,needH);
    CS.transW=needW; CS.transH=needH; CS.transInited=true;
    return CS.layerTransRT;
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

// ── Public API ────────────────────────────────────────────────────────

void Compositor_Init(void) {
    CS = {0};
}

void Compositor_Shutdown(void) {
    if(CS.transInited){ UnloadRenderTexture(CS.layerTransRT); CS.transInited=false; }
    if(CS.checkerValid){ UnloadTexture(CS.checkerTex); CS.checkerValid=false; }
    if(CS.shaderInited){ UnloadShader(CS.blendShader); CS.shaderInited=false; }
    if(CS.presentInited){ UnloadShader(CS.presentShader); CS.presentInited=false; }
    CS={0};
}

void Compositor_ReloadShader(void) {
    if(CS.shaderInited){ UnloadShader(CS.blendShader); CS.shaderInited=false; }
    LoadBlendShader();
}

void Compositor_BlitLayerOnto(
    Texture2D srcTex, const RectXform* xform,
    const CompositorBlendParams* params,
    const RectXform* viewXform,
    RenderTexture2D dst, Rectangle dstRegion)
{
    if(srcTex.id==0||dst.id==0) return;
    int sw=srcTex.width, sh=srcTex.height;
    if(sw<1||sh<1) return;
    EnsureShader();
    if(!CS.shaderInited) return;

    // Combined transform: output = viewXform->mat * xform->mat
    float cmb[6];
    Xform_Mul(cmb, viewXform->mat, xform->mat);

    if(params->seamless) {
        // 3×3 tile — render directly into dst at dstRegion
        RenderTexture2D transRT = GetTransRT(sw, sh);
        RenderTexture2D bufA=Load16BitRT((int)dstRegion.width,(int)dstRegion.height);
        RenderTexture2D bufB=Load16BitRT((int)dstRegion.width,(int)dstRegion.height);
        if(bufA.id==0||bufB.id==0){
            if(bufA.id>0)UnloadRenderTexture(bufA);
            if(bufB.id>0)UnloadRenderTexture(bufB);
            return;
        }
        // Seed with dst content at dstRegion
        CopyRT(bufA, dst, (int)dstRegion.width, (int)dstRegion.height);
        BlendSeamlessTiles(bufA,bufB,srcTex,cmb,sw,sh,
            (int)dstRegion.width,(int)dstRegion.height,
            params->opacity,params->blendMode,params->threshold,params->feather,transRT);
        // Copy result back to dst at dstRegion
        BeginTextureMode(dst);
        rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
        DrawTextureRec(bufA.texture,FullRect((int)dstRegion.width,(int)dstRegion.height),
            Vector2{dstRegion.x,dstRegion.y},WHITE);
        rlSetBlendMode(RL_BLEND_ALPHA);
        EndTextureMode();
        UnloadRenderTexture(bufA); UnloadRenderTexture(bufB);
        return;
    }

    // Non-seamless: compute AABB, clip to dstRegion, rectify, blend
    RectXform rx;
    memcpy(rx.mat, cmb, sizeof(cmb));
    rx.w = xform->w; rx.h = xform->h;
    Rectangle aabb = GetWorldAABB(&rx);
    // Clip aabb to dstRegion
    float l = fmaxf(aabb.x, dstRegion.x);
    float t = fmaxf(aabb.y, dstRegion.y);
    float r = fminf(aabb.x + aabb.width, dstRegion.x + dstRegion.width);
    float b = fminf(aabb.y + aabb.height, dstRegion.y + dstRegion.height);
    if(l>=r||t>=b) return;
    int clipW=(int)(r-l), clipH=(int)(b-t);
    if(clipW<1||clipH<1) return;

    // Offset combined transform by the clip offset
    float clipMat[6];
    memcpy(clipMat, cmb, sizeof(cmb));
    clipMat[2] -= l;
    clipMat[5] -= t;

    RenderTexture2D transRT = GetTransRT(clipW, clipH);
    BakeTransform(transRT, srcTex, clipMat, sw, sh);

    // Blend into dst at clip position using dst's existing content as base
    // We need a temp RT for the blend output, size = clipW x clipH
    RenderTexture2D baseBuf = Load16BitRT(clipW, clipH);
    if(baseBuf.id==0) return;
    // Copy dst clip region into baseBuf (correct underlay for blend)
    BeginTextureMode(baseBuf);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground(BLANK);
    DrawTextureRec(dst.texture, Rectangle{l, t, (float)clipW, (float)-clipH}, Vector2{0,0}, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    // Blend layer onto baseBuf, result into blendBuf
    RenderTexture2D blendBuf = Load16BitRT(clipW, clipH);
    if(blendBuf.id==0) { UnloadRenderTexture(baseBuf); return; }
    ApplyBlend(blendBuf, baseBuf.texture, transRT.texture,
        params->opacity,params->blendMode,params->threshold,params->feather,clipW,clipH);
    UnloadRenderTexture(baseBuf);
    // Copy blend result into dst at clip position
    BeginTextureMode(dst);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    DrawTextureRec(blendBuf.texture, FullRect(clipW,clipH), Vector2{l,t}, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadRenderTexture(blendBuf);
}

void Compositor_ApplyLayerToLayer(
    Texture2D topTex, const RectXform* topXform,
    const CompositorBlendParams* params,
    RenderTexture2D bottomRT, const RectXform* bottomXform)
{
    if(topTex.id==0||bottomRT.id==0) return;
    int bw=bottomRT.texture.width, bh=bottomRT.texture.height;
    if(bw<1||bh<1) return;

    // Compute relative transform: top-local → bottom-local
    float relMat[6];
    Xform_MulInv(relMat, topXform->mat, bottomXform->mat);

    // Build viewXform from relMat
    RectXform viewXf;
    memcpy(viewXf.mat, relMat, sizeof(relMat));
    viewXf.w = topXform->w;
    viewXf.h = topXform->h;

    // Identity layer xform — the viewXform already incorporates both
    RectXform identity = {};
    Xform_Identity(identity.mat);

    Compositor_BlitLayerOnto(topTex, &identity, params, &viewXf,
        bottomRT, Rectangle{0,0,(float)bw,(float)bh});
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
    return CS.checkerTex;
}
