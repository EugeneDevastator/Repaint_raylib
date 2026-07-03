#include "viewport_manager.h"
#include "compositor.h"
#include "layerstack.h"
#include "render_utils.h"
#include "undo.h"
#include "texture_manager.h"
#include "rlgl.h"
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    RenderTexture2D accumA, accumB;
    bool accumInited;
    int curW, curH;
    RenderTexture2D* finalAcc;
    bool dirty;
} VM;

bool layersDirty = true;

static int CW(void) { return LayerStack_RenderW(); }
static int CH(void) { return LayerStack_RenderH(); }

static Rectangle FullRect(int w, int h) { return Rectangle{0,0,(float)w,(float)-h}; }

static void EnsureAccumulators(int w, int h) {
    if(VM.accumInited&&VM.curW==w&&VM.curH==h) return;
    if(VM.accumInited){ UnloadRenderTexture(VM.accumA); UnloadRenderTexture(VM.accumB); }
    VM.accumA=Load16BitRT(w,h); VM.accumB=Load16BitRT(w,h);
    VM.curW=w; VM.curH=h; VM.accumInited=true; VM.finalAcc=NULL; VM.dirty=true;
}

// ── Init / shutdown ──────────────────────────────────────────────────
void ViewportManager_Init(void) { VM = {0}; }

void ViewportManager_Shutdown(void) {
    if(VM.accumInited){ UnloadRenderTexture(VM.accumA); UnloadRenderTexture(VM.accumB); VM.accumInited=false; }
    VM={0};
}

void ViewportManager_ReloadShader(void) {
    Compositor_ReloadShader();
    VM.dirty = true;
}

void ViewportManager_SetDirty(void) { VM.dirty = true; }

// ── Composite helpers ────────────────────────────────────────────────
RenderTexture2D* ViewportManager_Composite(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return NULL;
    EnsureAccumulators(cw,ch);
    if(!(VM.dirty||layersDirty)){ rlSetBlendMode(RL_BLEND_ALPHA); return (VM.accumInited&&VM.finalAcc)?VM.finalAcc:NULL; }
    VM.dirty=false; layersDirty=false;

    // Seed with checker
    Compositor_EnsureChecker(cw, ch);
    BeginTextureMode(VM.accumA); ClearBackground(BLANK);
    Texture2D ck = Compositor_GetCheckerTex();
    if(ck.id>0) DrawTexture(ck,0,0,WHITE);
    EndTextureMode();

    // Composite each visible layer via Compositor_BlitLayerOnto
    RectXform viewXform;
    const float* cv = LayerStack_GetCanvasView();
    memcpy(viewXform.mat, cv, 6*sizeof(float));
    viewXform.ww=0; viewXform.wh=0;

    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        RenderTexture2D rt = LayerStack_GetRT(i);
        if(rt.id==0) continue;
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;

        Compositor_BlitLayerOnto(rt.texture, &p->xform, &bp, &viewXform,
            VM.accumA, Rectangle{0,0,(float)cw,(float)ch});
    }
    VM.finalAcc = &VM.accumA;
    rlSetBlendMode(RL_BLEND_ALPHA);
    return (VM.accumInited&&VM.finalAcc)?VM.finalAcc:NULL;
}

Image ViewportManager_CompositeWithDither(void) {
    int cw=CW(),ch=CH(); if(cw<1||ch<1) return (Image){0};
    // Render all layers into a fresh RT
    RenderTexture2D a=Load16BitRT(cw,ch), b=Load16BitRT(cw,ch);
    if(a.id==0||b.id==0){ if(a.id>0)UnloadRenderTexture(a); if(b.id>0)UnloadRenderTexture(b); return (Image){0}; }
    BeginTextureMode(a); ClearBackground(BLANK); EndTextureMode();
    RectXform viewXform;
    const float* cv = LayerStack_GetCanvasView();
    memcpy(viewXform.mat, cv, 6*sizeof(float));
    viewXform.ww=0; viewXform.wh=0;

    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        RenderTexture2D rt = LayerStack_GetRT(i);
        if(rt.id==0) continue;
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;
        Compositor_BlitLayerOnto(rt.texture, &p->xform, &bp, &viewXform,
            a, Rectangle{0,0,(float)cw,(float)ch});
    }

    // Apply dither via present shader
    bool usePresent = Compositor_PresentInited();
    if(usePresent){
        BeginTextureMode(b); ClearBackground(BLANK);
        BeginShaderMode(Compositor_GetPresentShader());
        Compositor_SetPresentTexSize(cw, ch);
        Compositor_SetPresentDither(true);
        DrawTextureRec(a.texture, FullRect(cw,ch), Vector2{0,0}, WHITE);
        EndShaderMode(); EndTextureMode();
    } else {
        RenderTexture2D t=a; a=b; b=t;
    }

    Image result=LoadImageFromTexture(b.texture); ImageFlipVertical(&result);
    ImageFormat(&result,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    UnloadRenderTexture(a); UnloadRenderTexture(b);
    return result;
}

void ViewportManager_CompositeViewInto(RenderTexture2D dst, const RectXform* viewXform, int w, int h, const Rectangle* checkerRect) {
    if(w<1||h<1||dst.id==0) return;
    int cw=LayerStack_RenderW(), ch=LayerStack_RenderH();

    BeginTextureMode(dst); ClearBackground(BLANK);

    // Draw checker in crop region area of viewport
    if(checkerRect && checkerRect->width>0 && checkerRect->height>0 && cw>0 && ch>0) {
        Compositor_EnsureChecker(cw, ch);
        Texture2D ck = Compositor_GetCheckerTex();
        if(ck.id>0)
            DrawTexturePro(ck, Rectangle{0,0,(float)cw,(float)ch},
                *checkerRect, Vector2{0,0}, 0, WHITE);
    }

    EndTextureMode();

    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        RenderTexture2D rt = LayerStack_GetRT(i);
        if(rt.id==0) continue;
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;
        Compositor_BlitLayerOnto(rt.texture, &p->xform, &bp, viewXform,
            dst, Rectangle{0,0,(float)w,(float)h});
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
}

// ── Merge-down ────────────────────────────────────────────────────────
static bool IsRTShared(int idx) {
    RenderTexture2D rt = LayerStack_GetRT(idx);
    if(!rt.id) return false;
    for(int j=0;j<LayerStack_Count();j++)
        if(j!=idx && LayerStack_GetRT(j).id==rt.id) return true;
    return false;
}

// Shared blit: composites top layer onto layer below using top's properties.
// Does NOT delete the top layer — caller decides.
void ViewportManager_BlitLayerToLayer(int idx) {
    if(idx<=0||idx>=LayerStack_Count()) return;
    sLayerProps* top = LayerStack_GetProps(idx);
    sLayerProps* bot = LayerStack_GetProps(idx-1);
    RenderTexture2D topRT = LayerStack_GetRT(idx);
    RenderTexture2D botRT = LayerStack_GetRT(idx-1);
    if(topRT.id==0||botRT.id==0) return;

    CompositorBlendParams bp;
    bp.opacity=top->op; bp.blendMode=top->blendmode;
    bp.threshold=top->threshold; bp.feather=top->feather;
    bp.seamless=top->seamless;

    Compositor_ApplyLayerToLayer(topRT.texture, &top->xform, &bp, botRT, &bot->xform);
    ViewportManager_SetDirty();
}

void ViewportManager_MergeDown(int idx) {
    if(idx<=0||idx>=LayerStack_Count()) return;
    ViewportManager_BlitLayerToLayer(idx);
    sLayerProps* top = LayerStack_GetProps(idx);
    TexSlotID sidTop = LayerStack_GetSlotID(idx);
    TexSlotID sidBot = LayerStack_GetSlotID(idx-1);
    LayerStack_DeleteLayer(idx);
    if (g_undoManager) {
        g_undoManager->InvalidateSlot(sidTop);
        g_undoManager->InvalidateSlot(sidBot);
    }
    ViewportManager_SetDirty();
}

void ViewportManager_MergeDownSeamless(int idx) {
    // seamless is now handled inside BlitLayerToLayer via top->seamless
    ViewportManager_MergeDown(idx);
}

int ViewportManager_AcceptMatte(int srcIdx, Image matteImage) {
    int n = LayerStack_Count();
    if (srcIdx < 0 || srcIdx >= n || !matteImage.data) return -1;
    LayerStack_DuplicateLayer(srcIdx);
    int newIdx = srcIdx + 1;
    LayerStack_UploadToGPU(newIdx, matteImage);
    ViewportManager_SetDirty();
    return newIdx;
}

RenderTexture2D ViewportManager_GetMergedTexture(const RectXform* xform, int w, int h) {
    RenderTexture2D rt = LoadRenderTexture(w, h);
    ViewportManager_CompositeViewInto(rt, xform, w, h);
    return rt;
}

int ViewportManager_CreateLayerFromImage(Image img) {
    if (!img.data) return -1;

    int n = LayerStack_Count();
    int newIdx = LayerStack_InsertLayer(n > 0 ? n - 1 : 0);
    sLayerProps* lp = LayerStack_GetProps(newIdx);

    /* xform extent in world units (1 unit = WORLD_UNIT_PX px) */
    lp->xform = RectXform_Pivot(0, 0,
        (float)img.width / WORLD_UNIT_PX,
        (float)img.height / WORLD_UNIT_PX, 0);

    /* Upload image content */
    Texture2D tmp = LoadTextureFromImage(img);
    UnloadImage(img);
    BeginTextureMode(LayerStack_GetRT(newIdx));
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);

    ViewportManager_SetDirty();
    return newIdx;
}
