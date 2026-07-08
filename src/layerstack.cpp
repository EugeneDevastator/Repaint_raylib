#include "layerstack.h"
#include "undo.h"
#include "render_utils.h"
#include "RaylibUtils.h"
#include "texture_manager.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    sLayerProps* prop;
    RenderTexture2D* rt;
    TexSlotID* slotID;
    int count;
    RenderTexture2D canvasRT;
    TexSlotID       canvasSlot;
    RectXform       canvasXform;  // world-region xform for new layers
    float canvasView[6];
} LS;

// ── Helper: unload a single slot's GPU/CPU resources (no array shift) ──
static void UnloadLayerSlotResources(int idx) {
    if (TM_IsValid(LS.slotID[idx])) {
        bool shared = false;
        TexSlotID sid = LS.slotID[idx];
        for (int j = 0; j < LS.count; j++) {
            if (j != idx && LS.slotID[j].bucket == sid.bucket && LS.slotID[j].slot == sid.slot) {
                shared = true;
                break;
            }
        }
        if (!shared) { TM_Remove(LS.slotID[idx]); }
        LS.slotID[idx] = TM_INVALID_SLOT;
    }
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

// ── Helper: shift slot array entries up ──
static void ShiftLayersUp(int from, int to) {
    for(int i=from;i>to;i--){
        LS.prop[i]=LS.prop[i-1];
        LS.rt[i]=LS.rt[i-1];
        LS.slotID[i]=LS.slotID[i-1];
    }
}

// ── Helper: shift slot array entries down ──
static void ShiftLayersDown(int from, int to) {
    for(int i=from;i<to;i++){
        LS.prop[i]=LS.prop[i+1];
        LS.rt[i]=LS.rt[i+1];
        LS.slotID[i]=LS.slotID[i+1];
    }
}

// ── Helper: init a new slot with a blank image/RT ──
static void InitLayerSlot(int idx, int w, int h) {
    LS.rt[idx]=Load16BitRT(w,h);
    SetTextureFilter(LS.rt[idx].texture, TEXTURE_FILTER_BILINEAR);
    BeginTextureMode(LS.rt[idx]); ClearBackground(BLANK); EndTextureMode();
    LS.prop[idx]={}; LS.prop[idx].op=1; LS.prop[idx].visible=true;
    LS.prop[idx].blendmode=bmGamma;
    // Layer xform matches the canvas world-region, not the pixel size of the RT
    LS.prop[idx].xform = LS.canvasXform;
    if (LS.canvasXform.ww <= 0.0f || LS.canvasXform.wh <= 0.0f) {
        LS.prop[idx].xform.ww = (float)w;
        LS.prop[idx].xform.wh = (float)h;
    }
    LS.prop[idx].xform.mat[0]=1; LS.prop[idx].xform.mat[1]=0;
    LS.prop[idx].xform.mat[2]=0; LS.prop[idx].xform.mat[3]=0;
    LS.prop[idx].xform.mat[4]=1; LS.prop[idx].xform.mat[5]=0;
    LS.prop[idx].threshold=0; LS.prop[idx].feather=1;
    char name[64]; snprintf(name, sizeof(name), "Layer %d", idx);
    LS.slotID[idx]=TM_Register(TM_BUCKET_LAYER, LS.rt[idx], name, false, w, h);
}

// ── Helper: remove a layer slot (unload resources + shift down) ──
static void RemoveLayerSlot(int idx) {
    UnloadLayerSlotResources(idx);
    ShiftLayersDown(idx,LS.count-1);
    LS.count--;
}

// ── Init / shutdown ──────────────────────────────────────────────────
void LayerStack_Init(void) {
    LS = {0}; LS.canvasView[0]=1; LS.canvasView[4]=1;
}

void LayerStack_Shutdown(void) {
    for(int i=0;i<LS.count;i++) UnloadLayerSlotResources(i);
    free(LS.prop); free(LS.rt); free(LS.slotID);
    if(LS.canvasRT.id>0) UnloadRenderTexture(LS.canvasRT);
    LS.canvasRT={0}; LS.canvasSlot=TM_INVALID_SLOT;
    LS={0};
}

void LayerStack_InitCanvas(int w, int h) {
    if(LS.canvasRT.id>0) UnloadRenderTexture(LS.canvasRT);
    LS.canvasRT=Load16BitRT(w,h);
    if(LS.canvasRT.id>0)
        LS.canvasSlot=TM_Register(TM_BUCKET_LAYER, LS.canvasRT, "Canvas", true, w, h);
}

void LayerStack_SetCanvasXform(const RectXform* xf) {
    if (xf) LS.canvasXform = *xf;
}

void LayerStack_ResizeCanvas(int newW, int newH) {
    if(LS.canvasRT.id==0||newW<1||newH<1) return;
    int oldW=(int)LS.canvasRT.texture.width, oldH=(int)LS.canvasRT.texture.height;
    if(oldW==newW&&oldH==newH) return;
    RenderTexture2D newRT=Load16BitRT(newW,newH);
    if(newRT.id==0) return;
    BeginTextureMode(newRT); ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    DrawTexturePro(LS.canvasRT.texture, Rectangle{0,0,(float)oldW,(float)-oldH},
        Rectangle{0,0,(float)newW,(float)newH}, Vector2{0,0}, 0.0f, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA); EndTextureMode();
    if(TM_IsValid(LS.canvasSlot)) TM_Remove(LS.canvasSlot);
    UnloadRenderTexture(LS.canvasRT);
    LS.canvasRT=newRT;
    LS.canvasSlot=TM_Register(TM_BUCKET_LAYER, LS.canvasRT, "Canvas", true, newW, newH);
}

RenderTexture2D LayerStack_GetCanvasRT(void) { return LS.canvasRT; }
RenderTexture2D* LayerStack_GetCanvasRTPtr(void) { return &LS.canvasRT; }
int LayerStack_RenderW(void) { return LS.canvasRT.id>0?(int)LS.canvasRT.texture.width:0; }
int LayerStack_RenderH(void) { return LS.canvasRT.id>0?(int)LS.canvasRT.texture.height:0; }

void LayerStack_SetCanvasView(const float mat[6]) {
    memcpy(LS.canvasView, mat, 6*sizeof(float));
}

const float* LayerStack_GetCanvasView(void) { return LS.canvasView; }

// ── Accessors ─────────────────────────────────────────────────────────
int LayerStack_Count(void) { return LS.count; }
sLayerProps*   LayerStack_GetProps(int i){ return (i>=0&&i<LS.count)?&LS.prop[i]:NULL; }
RenderTexture2D LayerStack_GetRT(int i)   { return (i>=0&&i<LS.count)?LS.rt[i]:RenderTexture2D{0}; }
TexSlotID       LayerStack_GetSlotID(int i){ return (i>=0&&i<LS.count)?LS.slotID[i]:TM_INVALID_SLOT; }
int             LayerStack_FindLayerBySlot(TexSlotID slot) {
    if (!TM_IsValid(slot)) return -1;
    for (int i = 0; i < LS.count; i++)
        if (LS.slotID[i].bucket == slot.bucket && LS.slotID[i].slot == slot.slot) return i;
    return -1;
}

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
    int idx=LS.count; ReallocArrays(idx+1);
    InitLayerSlot(idx,w,h); LS.count++; return idx;
}

int LayerStack_InsertLayer(int afterIdx) {
    int idx=afterIdx<0?0:afterIdx>LS.count?LS.count:afterIdx;
    int cw=LayerStack_RenderW()>0?LayerStack_RenderW():512;
    int ch=LayerStack_RenderH()>0?LayerStack_RenderH():512;
    ReallocArrays(LS.count+1); ShiftLayersUp(LS.count,idx);
    InitLayerSlot(idx,cw,ch); LS.count++; return idx;
}

void LayerStack_DeleteLayer(int idx) {
    if(idx<0||idx>=LS.count||LS.count<=1) return;
    TexSlotID sid = LS.slotID[idx];
    RemoveLayerSlot(idx);
    if (g_undoManager && TM_IsValid(sid)) g_undoManager->InvalidateSlot(sid);
}

void LayerStack_DuplicateLayer(int idx) {
    if(idx<0||idx>=LS.count) return;
    int n=LS.count; ReallocArrays(n+1); ShiftLayersUp(n,idx+1);
    int di=idx+1; LS.prop[di]=LS.prop[idx];
    int lw=GetLayerWpx(idx), lh=GetLayerHpx(idx);
    LS.rt[di]=Load16BitRT(lw,lh);
    BeginTextureMode(LS.rt[di]);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground(BLANK);
    DrawTextureRec(LS.rt[idx].texture,Rectangle{0,0,(float)lw,(float)-lh},Vector2{0,0},WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    char name[64]; snprintf(name, sizeof(name), "Layer %d (dup)", di);
    LS.slotID[di]=TM_Register(TM_BUCKET_LAYER, LS.rt[di], name, false, lw, lh);
    LS.count++;
}

void LayerStack_DuplicateAsInstance(int idx) {
    if(idx<0||idx>=LS.count) return;
    int n=LS.count; ReallocArrays(n+1); ShiftLayersUp(n,idx+1);
    int di=idx+1; LS.prop[di]=LS.prop[idx];
    LS.prop[di].instanced=true;
    snprintf(LS.prop[di].layerName, sizeof(LS.prop[di].layerName), "%s(INST)", LS.prop[idx].layerName);
    LS.rt[di]=LS.rt[idx];
    LS.slotID[di]=LS.slotID[idx]; TM_AddRef(LS.slotID[idx]);
    LS.count++;
}

void LayerStack_MoveLayer(int from, int to) {
    if(from<0||from>=LS.count||to<0||to>=LS.count||from==to) return;
    RenderTexture2D mvRT=LS.rt[from]; sLayerProps mvProp=LS.prop[from]; TexSlotID mvSlot=LS.slotID[from];
    if(from<to) ShiftLayersDown(from,to); else ShiftLayersUp(from,to);
    LS.prop[to]=mvProp; LS.rt[to]=mvRT; LS.slotID[to]=mvSlot;
}

// ── GPU ↔ CPU transfer ────────────────────────────────────────────────
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
    BeginTextureMode(LS.rt[idx]); ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA); EndTextureMode();
    UnloadTexture(tmp); UnloadImage(img);
}

// ── Single-layer bake ────────────────────────────────────────────────
void LayerStack_BakeSingleLayer(int idx, RenderTexture2D dst) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0||dst.id==0) return;
    BeginTextureMode(dst); ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    DrawTextureRec(LS.rt[idx].texture, Rectangle{0,0,(float)GetLayerWpx(idx),(float)-GetLayerHpx(idx)}, Vector2{0,0}, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA); EndTextureMode();
}

// ── Scene bounds ─────────────────────────────────────────────────────
bool LayerStack_GetSceneBounds(Rectangle* out) {
    if(!out) return false;
    bool any=false; Rectangle aabb={0};
    for(int i=0;i<LS.count;i++){
        if(!LS.prop[i].visible||LS.rt[i].id==0) continue;
        Rectangle layerBB = GetWorldAABB(&LS.prop[i].xform);
        if(!any){ aabb=layerBB; any=true; }
        else{
            float l=fminf(aabb.x,layerBB.x), t=fminf(aabb.y,layerBB.y);
            float r=fmaxf(aabb.x+aabb.width,layerBB.x+layerBB.width);
            float b=fmaxf(aabb.y+aabb.height,layerBB.y+layerBB.height);
            aabb.x=l; aabb.y=t; aabb.width=r-l; aabb.height=b-t;
        }
    }
    *out=aabb; return any;
}

// ── Canvas-window commit ─────────────────────────────────────────────
void LayerStack_BakeCanvasWindow(const Document* doc) {
    (void)doc;
    for(int i=0;i<LS.count;i++){
        if(LS.rt[i].id==0) continue;
        float cmb[6]; Xform_Mul(cmb, LS.canvasView, LS.prop[i].xform.mat);
        memcpy(LS.prop[i].xform.mat, cmb, 6*sizeof(float));
    }
    Xform_Identity(LS.canvasView);
}

// ── Resize a layer's texture ──────────────────────────────────────────
void LayerStack_ResizeLayer(int idx, int newW, int newH) {
    if(idx<0||idx>=LS.count||LS.rt[idx].id==0||newW<1||newH<1) return;
    int oldW = GetLayerWpx(idx), oldH = GetLayerHpx(idx);
    if(oldW==newW && oldH==newH) return;

    RenderTexture2D newRT = Load16BitRT(newW, newH);
    if(newRT.id==0) return;
    BeginTextureMode(newRT);
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexturePro(LS.rt[idx].texture,
        Rectangle{0,0,(float)oldW,(float)-oldH},
        Rectangle{0,0,(float)newW,(float)newH},
        Vector2{0,0}, 0.0f, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();

    // Replace the slot — register new RT, unregister old
    {
        char name[64];
        snprintf(name, sizeof(name), "Layer %d", idx);
        TexSlotID newSlot = TM_Register(TM_BUCKET_LAYER, newRT, name, false, newW, newH);
        if(newSlot.slot >= 0) {
            TM_Remove(LS.slotID[idx]);
            RenderTexture2D oldRT = LS.rt[idx];
            LS.rt[idx] = newRT;
            LS.slotID[idx] = newSlot;
            UnloadRenderTexture(oldRT);
        } else {
            UnloadRenderTexture(newRT);
        }
    }
}
