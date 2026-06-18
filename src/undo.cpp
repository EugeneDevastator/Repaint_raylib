#include "undo.h"
#include "repaint.h"
#include "layerstack.h"
#include "texture_manager.h"
#include "rlgl.h"

static void UploadCPUToRT(TexSlot* ts) {
    Texture2D tmp = LoadTextureFromImage(ts->cpuImage);
    BeginTextureMode(ts->rt);
    ClearBackground(BLANK);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);
}

void UndoManager::Snapshot(AppState* state, TexSlotID slot) {
    (void)state;
    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return;
        LayerStack_SyncImageFromRT(li);
        Image* src = LayerStack_GetImage(li);
        if (!src || !src->data) return;
        UndoEntry entry;
        entry.snapshot = ImageCopy(*src);
        auto& st = m_undo[flatIdx(slot)];
        st.push_back(std::move(entry));
        while ((int)st.size() > MAX_UNDO) {
            st.front().Free();
            st.pop_front();
        }
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts || ts->rt.id == 0) return;
        Image cap = LoadImageFromTexture(ts->rt.texture);
        ImageFlipVertical(&cap);
        if (!cap.data) return;
        if (ts->cpuImage.data) UnloadImage(ts->cpuImage);
        ts->cpuImage = cap;
        UndoEntry entry;
        entry.snapshot = ImageCopy(ts->cpuImage);
        auto& st = m_undo[flatIdx(slot)];
        st.push_back(std::move(entry));
        while ((int)st.size() > MAX_UNDO) {
            st.front().Free();
            st.pop_front();
        }
    }
}

bool UndoManager::Undo(AppState* state, TexSlotID slot) {
    (void)state;
    auto& st = m_undo[flatIdx(slot)];
    if (st.empty()) return false;

    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return false;
        LayerStack_SyncImageFromRT(li);
        Image* cur = LayerStack_GetImage(li);
        if (!cur || !cur->data) return false;
        UndoEntry redo;
        redo.snapshot = ImageCopy(*cur);
        auto& rs = m_redo[flatIdx(slot)];
        rs.push_back(std::move(redo));
        UndoEntry& undo = st.back();
        UnloadImage(*cur);
        *cur = undo.snapshot;
        undo.snapshot.data = nullptr;
        undo.snapshot = {0};
        LayerStack_SyncRTFromImage(li);
        st.pop_back();
        return true;
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts) return false;
        Image cap = LoadImageFromTexture(ts->rt.texture);
        ImageFlipVertical(&cap);
        if (!cap.data) return false;
        if (ts->cpuImage.data) UnloadImage(ts->cpuImage);
        ts->cpuImage = cap;
        UndoEntry redo;
        redo.snapshot = ImageCopy(ts->cpuImage);
        auto& rs = m_redo[flatIdx(slot)];
        rs.push_back(std::move(redo));
        UndoEntry& undo = st.back();
        UnloadImage(ts->cpuImage);
        ts->cpuImage = undo.snapshot;
        undo.snapshot.data = nullptr;
        undo.snapshot = {0};
        UploadCPUToRT(ts);
        st.pop_back();
        return true;
    }
}

bool UndoManager::Redo(AppState* state, TexSlotID slot) {
    (void)state;
    auto& st = m_redo[flatIdx(slot)];
    if (st.empty()) return false;

    if (slot.bucket == TM_BUCKET_LAYER) {
        int li = LayerStack_FindLayerBySlot(slot);
        if (li < 0) return false;
        LayerStack_SyncImageFromRT(li);
        Image* cur = LayerStack_GetImage(li);
        if (!cur || !cur->data) return false;
        UndoEntry reundo;
        reundo.snapshot = ImageCopy(*cur);
        auto& us = m_undo[flatIdx(slot)];
        us.push_back(std::move(reundo));
        UndoEntry& redo = st.back();
        UnloadImage(*cur);
        *cur = redo.snapshot;
        redo.snapshot.data = nullptr;
        redo.snapshot = {0};
        LayerStack_SyncRTFromImage(li);
        st.pop_back();
        return true;
    } else {
        TexSlot* ts = TM_Get(slot);
        if (!ts) return false;
        Image cap = LoadImageFromTexture(ts->rt.texture);
        ImageFlipVertical(&cap);
        if (!cap.data) return false;
        if (ts->cpuImage.data) UnloadImage(ts->cpuImage);
        ts->cpuImage = cap;
        UndoEntry reundo;
        reundo.snapshot = ImageCopy(ts->cpuImage);
        auto& us = m_undo[flatIdx(slot)];
        us.push_back(std::move(reundo));
        UndoEntry& redo = st.back();
        UnloadImage(ts->cpuImage);
        ts->cpuImage = redo.snapshot;
        redo.snapshot.data = nullptr;
        redo.snapshot = {0};
        UploadCPUToRT(ts);
        st.pop_back();
        return true;
    }
}

void UndoManager::InvalidateAll() {
    for (auto& st : m_undo)
        for (auto& e : st) e.Free();
    for (auto& st : m_redo)
        for (auto& e : st) e.Free();
    for (auto& st : m_undo) st.clear();
    for (auto& st : m_redo) st.clear();
}
