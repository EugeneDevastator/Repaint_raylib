#include "layerstack.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    Image*  img;
    sLayerProps* prop;
    RenderTexture2D* rt;
    Texture2D* tex;
    int count;
    int renderW, renderH;
    RenderTexture2D accumA, accumB, layerTransRT;
    bool accumInited;
    Texture2D checkerTex; bool checkerValid;
    Shader blendShader; bool shaderInited;
    int locLayerTex, locLayerAlpha, locBmIdx, locLayerThreshold, locLayerFeather;
    Shader presentShader; bool presentInited;
    int curCanvasW, curCanvasH;
    RenderTexture2D* finalAcc;
    bool dirty;
} LS = {0};

static int CW(void) { return LS.renderW; }
static int CH(void) { return LS.renderH; }
static Rectangle FullRect(int w, int h) { return Rectangle{0,0,(float)w,(float)-h}; }

RenderTexture2D Load16BitRT(int w, int h) {
    RenderTexture2D t={0}; t.id=rlLoadFramebuffer();
    if(t.id>0){ rlEnableFramebuffer(t.id);
        t.texture.id=rlLoadTexture(NULL,w,h,PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,1);
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
    LS.blendShader=LoadShader(0,fs);
    if(LS.blendShader.id==0){ TraceLog(LOG_ERROR,"layer_blend.fs failed"); return false; }
    LS.locLayerTex=GetShaderLocation(LS.blendShader,"layerTex");
    LS.locLayerAlpha=GetShaderLocation(LS.blendShader,"layerAlpha");
    LS.locBmIdx=GetShaderLocation(LS.blendShader,"bmidx");
    LS.locLayerThreshold=GetShaderLocation(LS.blendShader,"layerThreshold");
    LS.locLayerFeather=GetShaderLocation(LS.blendShader,"layerFeather");
    LS.shaderInited=true; return true;
}

// ── Helper: unload a single slot's GPU/CPU resources (no array shift) ──
static void UnloadLayerSlotResources(int idx) {
    if(LS.rt[idx].id>0)UnloadRenderTexture(LS.rt[idx]);
    if(LS.tex[idx].id>0)UnloadTexture(LS.tex[idx]);
    UnloadImage(LS.img[idx]);
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
        LS.img[i]=LS.img[i-1]; LS.prop[i]=LS.prop[i-1];
        LS.rt[i]=LS.rt[i-1]; LS.tex[i]=LS.tex[i-1];
    }
}

// ── Helper: shift slot array entries down (close gap at `from`) ──
static void ShiftLayersDown(int from, int to) {
    for(int i=from;i<to;i++){
        LS.img[i]=LS.img[i+1]; LS.prop[i]=LS.prop[i+1];
        LS.rt[i]=LS.rt[i+1]; LS.tex[i]=LS.tex[i+1];
    }
}

// ── Helper: init a new slot with a blank image/RT at the given size ──
static void InitLayerSlot(int idx, int w, int h) {
    LS.img[idx]=GenImageColor(w,h,BLANK);
    ImageFormat(&LS.img[idx],PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    LS.rt[idx]=Load16BitRT(w,h);
    BeginTextureMode(LS.rt[idx]); ClearBackground(BLANK); EndTextureMode();
    LS.tex[idx]=LoadTextureFromImage(LS.img[idx]);
    LS.prop[idx]={}; LS.prop[idx].op=1; LS.prop[idx].visible=true;
    LS.prop[idx].blendmode=bmGamma; LS.prop[idx].mat[0]=1; LS.prop[idx].mat[4]=1;
    LS.prop[idx].threshold=0; LS.prop[idx].feather=1;
    LS.prop[idx].layerW=w; LS.prop[idx].layerH=h;
}

// ── Helper: rebuild tex[idx] from img[idx] with aliasing guard ──
static void RebuildTex(int idx) {
    if(LS.tex[idx].id>0 && LS.tex[idx].id!=LS.rt[idx].texture.id)
        UnloadTexture(LS.tex[idx]);
    else if(LS.tex[idx].id==LS.rt[idx].texture.id)
        LS.tex[idx]=Texture2D{0};
    LS.tex[idx]=LoadTextureFromImage(LS.img[idx]);
}

// ── Helper: capture an RT, store into img[idx], rebuild tex[idx] ──
static void RebuildLayerImageAndTex(int idx, RenderTexture2D srcRT) {
    Image cap=LoadImageFromTexture(srcRT.texture); ImageFlipVertical(&cap);
    UnloadImage(LS.img[idx]); LS.img[idx]=cap;
    RebuildTex(idx);
}

// ── Helper: remove a layer slot (unload resources + shift down) ──
static void RemoveLayerSlot(int idx) {
    UnloadLayerSlotResources(idx);
    ShiftLayersDown(idx,LS.count-1);
    LS.count--; LS.dirty=true;
}

// ── Init / shutdown ──────────────────────────────────────────────────
void LayerStack_Init(void) { LS = {0}; }

void LayerStack_Shutdown(void) {
    if(LS.accumInited){ UnloadRenderTexture(LS.accumA); UnloadRenderTexture(LS.accumB); UnloadRenderTexture(LS.layerTransRT); LS.accumInited=false; }
    if(LS.checkerValid){ UnloadTexture(LS.checkerTex); LS.checkerValid=false; }
    if(LS.shaderInited){ UnloadShader(LS.blendShader); LS.shaderInited=false; }
    if(LS.presentInited){ UnloadShader(LS.presentShader); LS.presentInited=false; }
    for(int i=0;i<LS.count;i++) UnloadLayerSlotResources(i);
    free(LS.img); free(LS.prop); free(LS.rt); free(LS.tex);
    LS={0};
}

void LayerStack_ReloadShader(void) {
    if(LS.shaderInited){ UnloadShader(LS.blendShader); LS.shaderInited=false; LS.dirty=true; }
    LoadBlendShader();
}

void LayerStack_SetRenderWindow(int w, int h) { LS.renderW=w; LS.renderH=h; LS.dirty=true; }

// ── Accessors ─────────────────────────────────────────────────────────
int LayerStack_Count(void) { return LS.count; }
sLayerProps*   LayerStack_GetProps(int i){ return (i>=0&&i<LS.count)?&LS.prop[i]:NULL; }
Image*         LayerStack_GetImage(int i){ return (i>=0&&i<LS.count)?&LS.img[i]:NULL; }
RenderTexture2D LayerStack_GetRT(int i)   { return (i>=0&&i<LS.count)?LS.rt[i]:RenderTexture2D{0}; }
Texture2D       LayerStack_GetTex(int i)  { return (i>=0&&i<LS.count)?LS.tex[i]:Texture2D{0}; }
int LayerStack_RenderW(void) { return LS.renderW; }
int LayerStack_RenderH(void) { return LS.renderH; }

// ── Internal array resize ────────────────────────────────────────────
static void ReallocArrays(int n) {
    LS.img=(Image*)realloc(LS.img,n*sizeof(Image));
    LS.prop=(sLayerProps*)realloc(LS.prop,n*sizeof(sLayerProps));
    LS.rt=(RenderTexture2D*)realloc(LS.rt,n*sizeof(RenderTexture2D));
    LS.tex=(Texture2D*)realloc(LS.tex,n*sizeof(Texture2D));
    if(n>LS.count){
        memset(&LS.rt[LS.count],0,(n-LS.count)*sizeof(RenderTexture2D));
        memset(&LS.tex[LS.count],0,(n-LS.count)*sizeof(Texture2D));
    }
}

// ── Layer management ─────────────────────────────────────────────────
int LayerStack_Add(int w, int h) {
    int idx=LS.count;
    ReallocArrays(idx+1);
    InitLayerSlot(idx,w,h);
    LS.count++; LS.dirty=true; return idx;
}

int LayerStack_InsertLayer(int afterIdx) {
    int idx=afterIdx<0?0:afterIdx>LS.count?LS.count:afterIdx;
    int cw=LS.renderW>0?LS.renderW:512, ch=LS.renderH>0?LS.renderH:512;
    ReallocArrays(LS.count+1);
    ShiftLayersUp(LS.count,idx);
    InitLayerSlot(idx,cw,ch);
    LS.count++; LS.dirty=true; return idx;
}

void LayerStack_DeleteLayer(int idx) {
    if(idx<0||idx>=LS.count||LS.count<=1) return;
    RemoveLayerSlot(idx);
}

void LayerStack_DuplicateLayer(int idx) {
    if(idx<0||idx>=LS.count) return;
    int n=LS.count; ReallocArrays(n+1);
    ShiftLayersUp(n,idx+1);
    int di=idx+1;
    LS.img[di]=ImageCopy(LS.img[idx]); LS.prop[di]=LS.prop[idx];
    int lw=LS.prop[idx].layerW, lh=LS.prop[idx].layerH;
    LS.rt[di]=Load16BitRT(lw,lh);
    CopyRT(LS.rt[di],LS.rt[idx],lw,lh);
    LS.tex[di]=LoadTextureFromImage(LS.img[di]);
    LS.count++; LS.dirty=true;
}

void LayerStack_MoveLayer(int from, int to) {
    if(from<0||from>=LS.count||to<0||to>=LS.count||from==to) return;
    RenderTexture2D mvRT=LS.rt[from]; Texture2D mvTex=LS.tex[from];
    Image mvImg=LS.img[from]; sLayerProps mvProp=LS.prop[from];
    if(from<to) ShiftLayersDown(from,to);
    else        ShiftLayersUp(from,to);
    LS.img[to]=mvImg; LS.prop[to]=mvProp; LS.rt[to]=mvRT; LS.tex[to]=mvTex;
    LS.dirty=true;
}

void LayerStack_ApplyTransform(int idx, const float mat[6]) {
    if(idx<0||idx>=LS.count) return;
    sLayerProps*lp=&LS.prop[idx];
    float a=mat[0],b=mat[1],tx=mat[2],c=mat[3],d=mat[4],ty=mat[5];
    float ca=lp->mat[0],cb=lp->mat[1],ctx=lp->mat[2],cc=lp->mat[3],cd=lp->mat[4],cty=lp->mat[5];
    lp->mat[0]=a*ca+b*cc; lp->mat[1]=a*cb+b*cd; lp->mat[2]=a*ctx+b*cty+tx;
    lp->mat[3]=c*ca+d*cc; lp->mat[4]=c*cb+d*cd; lp->mat[5]=c*ctx+d*cty+ty;
    LS.dirty=true;
}

// ── Sync ──────────────────────────────────────────────────────────────
void LayerStack_SyncImageFromRT(int idx) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0) return;
    Image cap=LoadImageFromTexture(LS.rt[idx].texture); ImageFlipVertical(&cap);
    if(cap.format!=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16){
        int px=cap.width*cap.height; uint16_t*d=(uint16_t*)malloc(px*4*2); uint8_t*s=(uint8_t*)cap.data;
        for(int i=0;i<px;i++){ d[i*4]=s[i*4]*257; d[i*4+1]=s[i*4+1]*257; d[i*4+2]=s[i*4+2]*257; d[i*4+3]=s[i*4+3]*257; }
        free(cap.data); cap.data=d; cap.format=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    }
    UnloadImage(LS.img[idx]); LS.img[idx]=cap;
}

void LayerStack_SyncLayerTex(int idx) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0) return;
    LayerStack_SyncImageFromRT(idx);
    RebuildTex(idx);
}

void LayerStack_SyncRTFromImage(int idx) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0||!LS.img[idx].data) return;
    Texture2D tmp = LoadTextureFromImage(LS.img[idx]);
    BeginTextureMode(LS.rt[idx]);
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);
    RebuildTex(idx);
    LS.dirty=true;
}

// ── Bake / blend helpers ────────────────────────────────────────────
static void BakeTransform(RenderTexture2D dst, Texture2D src, const float mat[6], int lw, int lh, int cw, int ch) {
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    BeginTextureMode(dst); ClearBackground(BLANK);
    // Negative determinant = flip → triangle winding inverts → disable culling.
    // Must flush batch while culling is off — raylib batches draw calls and only
    // executes them on flush, so re-enabling before EndTextureMode's flush would
    // use the wrong state.
    bool flip = (mat[0]*mat[4] - mat[1]*mat[3]) < 0.0f;
    if (flip) { rlDisableBackfaceCulling(); rlDrawRenderBatchActive(); }
    rlPushMatrix();
    float m[16]={mat[0],mat[3],0,0, mat[1],mat[4],0,0, 0,0,1,0, mat[2],mat[5],0,1};
    rlMultMatrixf(m);
    DrawTextureRec(src,Rectangle{0,0,(float)lw,(float)-lh},Vector2{0,0},WHITE);
    rlPopMatrix();
    if (flip) { rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); }
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
    float relMat[6]; MatInvMul(LS.prop[idx-1].mat, LS.prop[idx].mat, relMat);
    if(LS.layerTransRT.id==0) return LS.rt[idx].texture;
    int cw=CW(),ch=CH(),lw=LS.prop[idx].layerW,lh=LS.prop[idx].layerH;
    BakeTransform(LS.layerTransRT,LS.rt[idx].texture,relMat,lw,lh,cw,ch);
    return LS.layerTransRT.texture;
}

// ── Merge down ────────────────────────────────────────────────────────
static void MergeDownImpl(int idx, bool seamless) {
    if(!LS.shaderInited||idx<=0||idx>=LS.count||LS.rt[idx].id==0||LS.rt[idx-1].id==0) return;
    int cw=CW(),ch=CH(),bw=LS.prop[idx-1].layerW,bh=LS.prop[idx-1].layerH;
    int lw=LS.prop[idx].layerW,lh=LS.prop[idx].layerH;
    sLayerProps*p=&LS.prop[idx];

    if(!seamless) {
        Texture2D topTex=GetTransformedTop(idx);
        RenderTexture2D mergedRT=Load16BitRT(bw,bh);
        ApplyBlendShader(mergedRT,LS.rt[idx-1].texture,topTex,p->op,p->blendmode,p->threshold,p->feather,bw,bh);
        RenderTexture2D oldRT=LS.rt[idx-1]; LS.rt[idx-1]=mergedRT;
        RebuildLayerImageAndTex(idx-1,mergedRT);
        UnloadRenderTexture(oldRT);
        RemoveLayerSlot(idx);
        return;
    }

    // ── Seamless: blend each tile individually for proper overlap ─────
    float relMat[6]; MatInvMul(LS.prop[idx-1].mat, LS.prop[idx].mat, relMat);
    Texture2D topTex=LS.rt[idx].texture;
    SetTextureWrap(topTex,TEXTURE_WRAP_REPEAT);

    RenderTexture2D bufA=Load16BitRT(bw,bh), bufB=Load16BitRT(bw,bh);
    if(bufA.id==0||bufB.id==0){ if(bufA.id>0)UnloadRenderTexture(bufA); if(bufB.id>0)UnloadRenderTexture(bufB); return; }

    // Seed bufA with the bottom layer
    CopyRT(bufA,LS.rt[idx-1],bw,bh);

    RenderTexture2D*src=&bufA,*dst=&bufB;
    for(int dy=-1;dy<=1;dy++){
        for(int dx=-1;dx<=1;dx++){
            float tileMat[6];
            memcpy(tileMat,relMat,6*sizeof(float));
            tileMat[2]+=dx*(float)lw; tileMat[5]+=dy*(float)lh;

            BakeTransform(LS.layerTransRT,topTex,tileMat,lw,lh,cw,ch);

            ApplyBlendShader(*dst,src->texture,LS.layerTransRT.texture,p->op,p->blendmode,p->threshold,p->feather,bw,bh);
            RenderTexture2D*tmp=src; src=dst; dst=tmp;
        }
    }

    // src holds the final blended result
    RenderTexture2D mergedRT=*src;
    UnloadRenderTexture(*dst);
    RenderTexture2D oldRT=LS.rt[idx-1]; LS.rt[idx-1]=mergedRT;
    RebuildLayerImageAndTex(idx-1,mergedRT);
    UnloadRenderTexture(oldRT);
    RemoveLayerSlot(idx);
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
    LS.presentShader=LoadShader(0,fs); LS.presentInited=LS.presentShader.id>0;
}

// ── Helper: run the ping-pong layer blend loop ──
// Returns pointer to whichever RT holds the final accumulated result.
static RenderTexture2D* CompositeLayersInto(RenderTexture2D& a, RenderTexture2D& b, int cw, int ch) {
    RenderTexture2D*src=&a,*dst=&b;
    for(int i=0;i<LS.count;i++){
        if(!LS.prop[i].visible||LS.rt[i].id==0) continue;
        Texture2D layerTex=LS.rt[i].texture;
        if(LS.layerTransRT.id>0){
            BakeTransform(LS.layerTransRT,LS.rt[i].texture,LS.prop[i].mat,LS.prop[i].layerW,LS.prop[i].layerH,cw,ch);
            layerTex=LS.layerTransRT.texture;
        }
        sLayerProps*p=&LS.prop[i];
        if(LS.shaderInited)
            ApplyBlendShader(*dst,src->texture,layerTex,p->op,p->blendmode,p->threshold,p->feather,cw,ch);
        else {
            BeginTextureMode(*dst); ClearBackground(BLANK);
            DrawTextureRec(src->texture,FullRect(cw,ch),Vector2{0,0},WHITE);
            DrawTextureRec(layerTex,FullRect(cw,ch),Vector2{0,0},ColorAlpha(WHITE,p->op));
            EndTextureMode();
        }
        RenderTexture2D*tmp=src; src=dst; dst=tmp;
    }
    return src;
}

bool LayerStack_PresentInited(void) { return LS.presentInited; }
Shader LayerStack_GetPresentShader(void) { return LS.presentShader; }
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
    DrawTextureRec(finalAcc->texture,FullRect(cw,ch),Vector2{0,0},WHITE);
    if(LS.presentInited)EndShaderMode(); EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    Image result=LoadImageFromTexture(out->texture); ImageFlipVertical(&result);
    ImageFormat(&result,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b); return result;
}

// ── New compositing API (caller-owned targets, arbitrary resolution) ──

static void _viewBlendLoop(RenderTexture2D dst, RenderTexture2D tmp,
                            const float viewMat[6], int w, int h) {
    RenderTexture2D* src=&dst;
    RenderTexture2D* dstBuf=&tmp;

    // Own transform RT sized to match the output — layerTransRT may be wrong size
    RenderTexture2D transRT={0};
    bool ownsTrans=false;
    if(LS.layerTransRT.id>0 && LS.layerTransRT.texture.width>=(unsigned)w && LS.layerTransRT.texture.height>=(unsigned)h)
        transRT=LS.layerTransRT;
    else{
        transRT=Load16BitRT(w,h);
        ownsTrans=true;
    }

    for(int i=0;i<LS.count;i++){
        if(!LS.prop[i].visible||LS.rt[i].id==0) continue;

        // Compute combined transform: viewMat * layerMat
        Texture2D layerTex=LS.rt[i].texture;
        if(transRT.id>0){
            float ca=LS.prop[i].mat[0],cb=LS.prop[i].mat[1],ctx=LS.prop[i].mat[2];
            float cc=LS.prop[i].mat[3],cd=LS.prop[i].mat[4],cty=LS.prop[i].mat[5];
            float cmb[6];
            if(viewMat){
                cmb[0]=viewMat[0]*ca+viewMat[1]*cc; cmb[1]=viewMat[0]*cb+viewMat[1]*cd; cmb[2]=viewMat[0]*ctx+viewMat[1]*cty+viewMat[2];
                cmb[3]=viewMat[3]*ca+viewMat[4]*cc; cmb[4]=viewMat[3]*cb+viewMat[4]*cd; cmb[5]=viewMat[3]*ctx+viewMat[4]*cty+viewMat[5];
            }else{
                cmb[0]=ca; cmb[1]=cb; cmb[2]=ctx; cmb[3]=cc; cmb[4]=cd; cmb[5]=cty;
            }
            BakeTransform(transRT,LS.rt[i].texture,cmb,LS.prop[i].layerW,LS.prop[i].layerH,w,h);
            layerTex=transRT.texture;
        }
        sLayerProps*p=&LS.prop[i];
        if(LS.shaderInited)
            ApplyBlendShader(*dstBuf,src->texture,layerTex,p->op,p->blendmode,p->threshold,p->feather,w,h);
        else{
            BeginTextureMode(*dstBuf); ClearBackground(BLANK);
            DrawTextureRec(src->texture,FullRect(w,h),Vector2{0,0},WHITE);
            DrawTextureRec(layerTex,FullRect(w,h),Vector2{0,0},ColorAlpha(WHITE,p->op));
            EndTextureMode();
        }
        RenderTexture2D*t=src; src=dstBuf; dstBuf=t;
    }
    // If final result is in tmp, copy to dst
    if(src!=&dst) CopyRT(dst,*src,w,h);
    if(ownsTrans) UnloadRenderTexture(transRT);
}

void LayerStack_ProduceCompositeView(RenderTexture2D dst, const float viewMat[6], int w, int h) {
    if(w<1||h<1||dst.id==0) return;
    EnsureShader();
    EnsureChecker(CW(),CH());  // ensure checker texture exists
    RenderTexture2D tmp=Load16BitRT(w,h);

    // Seed accumulator with checkerboard at canvas position — matches legacy
    // behaviour where the checkerboard is the first "layer" in the composite.
    BeginTextureMode(dst); ClearBackground(BLANK);
    if(LS.checkerTex.id>0 && CW()>0 && CH()>0){
        rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
        rlPushMatrix();
        float vm[6];
        if(viewMat){ vm[0]=viewMat[0]; vm[1]=viewMat[1]; vm[2]=viewMat[2]; vm[3]=viewMat[3]; vm[4]=viewMat[4]; vm[5]=viewMat[5]; }
        else{ vm[0]=1; vm[1]=0; vm[2]=0; vm[3]=0; vm[4]=1; vm[5]=0; }
        float m[16]={vm[0],vm[3],0,0, vm[1],vm[4],0,0, 0,0,1,0, vm[2],vm[5],0,1};
        rlMultMatrixf(m);
        DrawTextureRec(LS.checkerTex,FullRect(CW(),CH()),Vector2{0,0},WHITE);
        rlPopMatrix(); rlSetBlendMode(RL_BLEND_ALPHA);
    }
    EndTextureMode();

    _viewBlendLoop(dst,tmp,viewMat,w,h);
    UnloadRenderTexture(tmp);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

void LayerStack_ProduceComposite(RenderTexture2D dst, int w, int h) {
    LayerStack_ProduceCompositeView(dst,NULL,w,h);
}

void LayerStack_ProduceCompositeDitherView8b(Image* dst, const float viewMat[6], int w, int h) {
    if(w<1||h<1||!dst) return;
    EnsureShader(); EnsurePresentShader();
    RenderTexture2D a=Load16BitRT(w,h),b=Load16BitRT(w,h);
    BeginTextureMode(a); ClearBackground(BLANK); EndTextureMode();
    _viewBlendLoop(a,b,viewMat,w,h);
    // a holds the final 16-bit composite — apply present shader into b, read back as 8-bit
    BeginTextureMode(b); ClearBackground(BLANK);
    if(LS.presentInited)BeginShaderMode(LS.presentShader);
    DrawTextureRec(a.texture,FullRect(w,h),Vector2{0,0},WHITE);
    if(LS.presentInited)EndShaderMode(); EndTextureMode();
    *dst=LoadImageFromTexture(b.texture); ImageFlipVertical(dst);
    ImageFormat(dst,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

void LayerStack_ProduceCompositeDither8b(Image* dst, int w, int h) {
    LayerStack_ProduceCompositeDitherView8b(dst,NULL,w,h);
}


