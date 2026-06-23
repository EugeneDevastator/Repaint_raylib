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
    int renderW, renderH;
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
    LS.prop[idx].blendmode=bmGamma; LS.prop[idx].xform = RectXform_Pivot(0, 0, (float)w, (float)h, 0);
    LS.prop[idx].threshold=0; LS.prop[idx].feather=1;
    LS.prop[idx].layerW=w; LS.prop[idx].layerH=h;
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
void LayerStack_Init(void) { LS = {0}; LS.canvasView[0]=1; LS.canvasView[4]=1; }

void LayerStack_Shutdown(void) {
    for(int i=0;i<LS.count;i++) UnloadLayerSlotResources(i);
    free(LS.prop); free(LS.rt); free(LS.slotID);
    LS={0};
}

void LayerStack_SetRenderWindow(int w, int h) { LS.renderW=w; LS.renderH=h; }

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
    int idx=LS.count; ReallocArrays(idx+1);
    InitLayerSlot(idx,w,h); LS.count++; return idx;
}

int LayerStack_InsertLayer(int afterIdx) {
    int idx=afterIdx<0?0:afterIdx>LS.count?LS.count:afterIdx;
    int cw=LS.renderW>0?LS.renderW:512, ch=LS.renderH>0?LS.renderH:512;
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
    int lw=LS.prop[idx].layerW, lh=LS.prop[idx].layerH;
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
    DrawTextureRec(LS.rt[idx].texture, Rectangle{0,0,(float)LS.prop[idx].layerW,(float)-LS.prop[idx].layerH}, Vector2{0,0}, WHITE);
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
