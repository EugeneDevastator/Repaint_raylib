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
    bool dirty;
} VM;

bool layersDirty = true;

// ── Init / shutdown ──────────────────────────────────────────────────
void ViewportManager_Init(void) { VM = {0}; }
void ViewportManager_Shutdown(void) { VM = {0}; }

void ViewportManager_ReloadShader(void) {
    Compositor_ReloadShader();
    VM.dirty = true;
}

void ViewportManager_SetDirty(void) { VM.dirty = true; }

// ── Composite onto the canvas RT ─────────────────────────────────────
static Rectangle FullRect(int w, int h) { return Rectangle{0,0,(float)w,(float)-h}; }
RenderTexture2D* ViewportManager_Composite(void) {
    Quad* canvas = LayerStack_GetCanvasQuadPtr();
    if(!canvas || canvas->rt.id==0) return NULL;
    int cw = (int)canvas->rt.texture.width, ch = (int)canvas->rt.texture.height;
    if(cw<1||ch<1) return NULL;

    if(!(VM.dirty||layersDirty)) { rlSetBlendMode(RL_BLEND_ALPHA); return &canvas->rt; }
    VM.dirty=false; layersDirty=false;

    // Clear canvas with checker background
    Compositor_EnsureChecker(cw, ch);
    BeginTextureMode(canvas->rt); ClearBackground(BLANK);
    Texture2D ck = Compositor_GetCheckerTex();
    if(ck.id>0) DrawTexture(ck,0,0,WHITE);
    EndTextureMode();

    // Blit each visible layer onto the canvas via QuadApply
    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        Quad* layerQ = LayerStack_GetQuadPtr(i);
        if(!layerQ || layerQ->rt.id==0) continue;
        Quad src = *layerQ;               // copy RT from Quad
        src.xform = p->xform;              // use live xform from props
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;
        Compositor_QuadApply(&src, &bp, canvas);
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
    return &canvas->rt;
}

Image ViewportManager_CompositeWithDither(void) {
    int cw=LayerStack_RenderW(),ch=LayerStack_RenderH(); if(cw<1||ch<1) return (Image){0};
    // Render all layers into a fresh RT using the canvas as the view Quad
    RenderTexture2D a=Load16BitRT(cw,ch), b=Load16BitRT(cw,ch);
    if(a.id==0||b.id==0){ if(a.id>0)UnloadRenderTexture(a); if(b.id>0)UnloadRenderTexture(b); return (Image){0}; }
    BeginTextureMode(a); ClearBackground(BLANK); EndTextureMode();

    // Use canvas xform as the view; build a temp destination Quad for blitting
    Quad* canvas = LayerStack_GetCanvasQuadPtr();
    Quad dst = *canvas;
    dst.rt = a;

    int count = LayerStack_Count();
    for(int i=0;i<count;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        Quad* layerQ = LayerStack_GetQuadPtr(i);
        if(!layerQ || layerQ->rt.id==0) continue;
        Quad src = *layerQ;
        src.xform = p->xform;
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;
        Compositor_QuadApply(&src, &bp, &dst);
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

// ── Apply all visible layers onto a destination Quad ───────────────────
void ViewportManager_CompositeLayersOntoQuad(const Quad* dst) {
    int n = LayerStack_Count();
    for(int i=0;i<n;i++){
        sLayerProps* p = LayerStack_GetProps(i);
        if(!p||!p->visible) continue;
        Quad* lq = LayerStack_GetQuadPtr(i);
        if(!lq || lq->rt.id==0) continue;
        Quad src = *lq;
        src.xform = p->xform;
        CompositorBlendParams bp;
        bp.opacity=p->op; bp.blendMode=p->blendmode;
        bp.threshold=p->threshold; bp.feather=p->feather;
        bp.seamless=p->seamless;
        Compositor_QuadApply(&src, &bp, dst);
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

int ViewportManager_CreateLayerFromImage(Image img) {
    if (!img.data) return -1;

    int n = LayerStack_Count();
    // Create at the END with pixel-accurate RT and xform
    int newIdx = LayerStack_Add(img.width, img.height);
    sLayerProps* lp = LayerStack_GetProps(newIdx);

    // Upload image content
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

    // Move above bottommost layer
    if (n > 1) { LayerStack_MoveLayer(newIdx, n - 1); newIdx = n - 1; }

    ViewportManager_SetDirty();
    return newIdx;
}
