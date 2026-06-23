#include "undo.h"
#include "repaint.h"
#include "viewport_manager.h"
#include "render_utils.h"
#include "layerstack.h"
#include "texture_manager.h"
#include "rlgl.h"

static void CopyRT(RenderTexture2D dst, RenderTexture2D src, int w, int h) {
    BeginTextureMode(dst);
    rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground(BLANK);
    DrawTextureRec(src.texture, Rectangle{0,0,(float)w,(float)-h}, Vector2{0,0}, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
}

void UndoManager::Snapshot(AppState* state, TexSlotID slot) {
    (void)state;
    int w = 0, h = 0;
    RenderTexture2D srcRT = {0};

    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return;
        srcRT = LayerStack_GetRT(li);
        sLayerProps* p = LayerStack_GetProps(li);
        if (!p || srcRT.id == 0) return;
        w = p->layerW;
        h = p->layerH;
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts || ts->rt.id == 0) return;
        srcRT = ts->rt;
        w = ts->rt.texture.width;
        h = ts->rt.texture.height;
    }

    RenderTexture2D snapRT = Load16BitRT(w, h);
    if (snapRT.id == 0) return;
    CopyRT(snapRT, srcRT, w, h);

    UndoEntry entry;
    entry.snapshot = snapRT;
    auto& st = m_undo[flatIdx(slot)];
    st.push_back(std::move(entry));
    while ((int)st.size() > MAX_UNDO)
        st.pop_front();
}

bool UndoManager::Undo(AppState* state, TexSlotID slot) {
    (void)state;
    auto& st = m_undo[flatIdx(slot)];
    if (st.empty()) return false;

    int w = 0, h = 0;
    RenderTexture2D srcRT = {0};

    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return false;
        srcRT = LayerStack_GetRT(li);
        sLayerProps* p = LayerStack_GetProps(li);
        if (!p || srcRT.id == 0) return false;
        w = p->layerW;
        h = p->layerH;
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts || ts->rt.id == 0) return false;
        srcRT = ts->rt;
        w = ts->rt.texture.width;
        h = ts->rt.texture.height;
    }

    // Save current state as redo
    RenderTexture2D redoRT = Load16BitRT(w, h);
    if (redoRT.id == 0) return false;
    CopyRT(redoRT, srcRT, w, h);
    UndoEntry redo;
    redo.snapshot = redoRT;
    auto& rs = m_redo[flatIdx(slot)];
    rs.push_back(std::move(redo));

    // Restore from undo
    UndoEntry& undo = st.back();
    CopyRT(srcRT, undo.snapshot, w, h);
    st.pop_back();

    if (slot.bucket == TM_BUCKET_LAYER) ViewportManager_SetDirty();
    layersDirty = true;
    return true;
}

bool UndoManager::Redo(AppState* state, TexSlotID slot) {
    (void)state;
    auto& st = m_redo[flatIdx(slot)];
    if (st.empty()) return false;

    int w = 0, h = 0;
    RenderTexture2D srcRT = {0};

    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return false;
        srcRT = LayerStack_GetRT(li);
        sLayerProps* p = LayerStack_GetProps(li);
        if (!p || srcRT.id == 0) return false;
        w = p->layerW;
        h = p->layerH;
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts || ts->rt.id == 0) return false;
        srcRT = ts->rt;
        w = ts->rt.texture.width;
        h = ts->rt.texture.height;
    }

    // Save current state back to undo
    RenderTexture2D reundoRT = Load16BitRT(w, h);
    if (reundoRT.id == 0) return false;
    CopyRT(reundoRT, srcRT, w, h);
    UndoEntry reundo;
    reundo.snapshot = reundoRT;
    auto& us = m_undo[flatIdx(slot)];
    us.push_back(std::move(reundo));

    // Restore from redo
    UndoEntry& redo = st.back();
    CopyRT(srcRT, redo.snapshot, w, h);
    st.pop_back();

    if (slot.bucket == TM_BUCKET_LAYER) ViewportManager_SetDirty();
    layersDirty = true;
    return true;
}

void UndoManager::InvalidateAll() {
    for (auto& st : m_undo) st.clear();
    for (auto& st : m_redo) st.clear();
}

void UndoManager::InvalidateSlot(TexSlotID slot) {
    int fi = flatIdx(slot);
    if (fi >= 0 && fi < MAX_SLOTS) {
        m_undo[fi].clear();
        m_redo[fi].clear();
    }
}
