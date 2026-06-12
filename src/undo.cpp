#include "undo.h"
#include "repaint.h"
#include "layerstack.h"

// ── Helpers to sync a brush texture RT ↔ CPU image ────────────────────────
static void SyncBrushTexFromRT(AppState* state, int texIdx) {
    if (texIdx < 0 || texIdx >= state->brushTexCount) return;
    BrushTexture* bt = &state->brushTex[texIdx];
    if (bt->rt.id == 0) return;
    Image img = LoadImageFromTexture(bt->rt.texture);
    if (img.data) {
        if (bt->cpuImage.data) UnloadImage(bt->cpuImage);
        bt->cpuImage = img;
    }
}

static void SyncBrushTexFromImage(AppState* state, int texIdx) {
    if (texIdx < 0 || texIdx >= state->brushTexCount) return;
    BrushTexture* bt = &state->brushTex[texIdx];
    if (!bt->cpuImage.data || bt->rt.id == 0) return;
    Texture2D tmp = LoadTextureFromImage(bt->cpuImage);
    BeginTextureMode(bt->rt);
    ClearBackground(BLANK);
    DrawTexture(tmp, 0, 0, WHITE);
    EndTextureMode();
    UnloadTexture(tmp);
}

void UndoManager::Snapshot(AppState* state, int idx, bool isTexture) {
    if (isTexture) {
        if (idx < 0 || idx >= state->brushTexCount) return;
        if (state->brushTex[idx].rt.id == 0) return;
        SyncBrushTexFromRT(state, idx);
        Image* src = &state->brushTex[idx].cpuImage;
        if (!src || !src->data) return;
        UndoEntry entry;
        entry.snapshot = ImageCopy(*src);
        entry.layerIndex = idx;
        auto& st = m_texUndo[idx];
        st.push_back(std::move(entry));
        while ((int)st.size() > MAX_UNDO) {
            st.front().Free();
            st.pop_front();
        }
    } else {
        if (idx < 0 || idx >= LayerStack_Count()) return;
        LayerStack_SyncImageFromRT(idx);
        Image* src = LayerStack_GetImage(idx);
        if (!src || !src->data) return;
        UndoEntry entry;
        entry.snapshot = ImageCopy(*src);
        entry.layerIndex = idx;
        auto& st = m_undo[idx];
        st.push_back(std::move(entry));
        while ((int)st.size() > MAX_UNDO) {
            st.front().Free();
            st.pop_front();
        }
    }
}

bool UndoManager::Undo(AppState* state, int idx, bool isTexture) {
    if (isTexture) {
        if (idx < 0 || idx >= state->brushTexCount) return false;
        auto& st = m_texUndo[idx];
        if (st.empty()) return false;
        SyncBrushTexFromRT(state, idx);
        Image* cur = &state->brushTex[idx].cpuImage;
        if (!cur || !cur->data) return false;
        UndoEntry redo;
        redo.snapshot = ImageCopy(*cur);
        redo.layerIndex = idx;
        m_texRedo[idx].push_back(std::move(redo));
        UndoEntry& undo = st.back();
        UnloadImage(*cur);
        *cur = undo.snapshot;
        undo.snapshot.data = nullptr;
        undo.snapshot = {0};
        SyncBrushTexFromImage(state, idx);
        st.pop_back();
        return true;
    } else {
        if (idx < 0 || idx >= LayerStack_Count()) return false;
        auto& st = m_undo[idx];
        if (st.empty()) return false;
        LayerStack_SyncImageFromRT(idx);
        Image* cur = LayerStack_GetImage(idx);
        if (!cur || !cur->data) return false;
        UndoEntry redo;
        redo.snapshot = ImageCopy(*cur);
        redo.layerIndex = idx;
        m_redo[idx].push_back(std::move(redo));
        UndoEntry& undo = st.back();
        UnloadImage(*cur);
        *cur = undo.snapshot;
        undo.snapshot.data = nullptr;
        undo.snapshot = {0};
        LayerStack_SyncRTFromImage(idx);
        st.pop_back();
        return true;
    }
}

bool UndoManager::Redo(AppState* state, int idx, bool isTexture) {
    if (isTexture) {
        if (idx < 0 || idx >= state->brushTexCount) return false;
        auto& st = m_texRedo[idx];
        if (st.empty()) return false;
        SyncBrushTexFromRT(state, idx);
        Image* cur = &state->brushTex[idx].cpuImage;
        if (!cur || !cur->data) return false;
        UndoEntry reundo;
        reundo.snapshot = ImageCopy(*cur);
        reundo.layerIndex = idx;
        m_texUndo[idx].push_back(std::move(reundo));
        UndoEntry& redo = st.back();
        UnloadImage(*cur);
        *cur = redo.snapshot;
        redo.snapshot.data = nullptr;
        redo.snapshot = {0};
        SyncBrushTexFromImage(state, idx);
        st.pop_back();
        return true;
    } else {
        if (idx < 0 || idx >= LayerStack_Count()) return false;
        auto& st = m_redo[idx];
        if (st.empty()) return false;
        LayerStack_SyncImageFromRT(idx);
        Image* cur = LayerStack_GetImage(idx);
        if (!cur || !cur->data) return false;
        UndoEntry reundo;
        reundo.snapshot = ImageCopy(*cur);
        reundo.layerIndex = idx;
        m_undo[idx].push_back(std::move(reundo));
        UndoEntry& redo = st.back();
        UnloadImage(*cur);
        *cur = redo.snapshot;
        redo.snapshot.data = nullptr;
        redo.snapshot = {0};
        LayerStack_SyncRTFromImage(idx);
        st.pop_back();
        return true;
    }
}

void UndoManager::ClearRedo(int idx, bool isTexture) {
    if (idx < 0 || idx >= 256) return;
    auto& st = isTexture ? m_texRedo[idx] : m_redo[idx];
    for (auto& e : st) e.Free();
    st.clear();
}

void UndoManager::ClearLayer(int idx) {
    if (idx < 0 || idx >= 256) return;
    for (auto& e : m_undo[idx]) e.Free();
    m_undo[idx].clear();
    for (auto& e : m_redo[idx]) e.Free();
    m_redo[idx].clear();
    for (auto& e : m_texUndo[idx]) e.Free();
    m_texUndo[idx].clear();
    for (auto& e : m_texRedo[idx]) e.Free();
    m_texRedo[idx].clear();
}

void UndoManager::InvalidateAll() {
    for (auto& st : m_undo)
        for (auto& e : st) e.Free();
    for (auto& st : m_redo)
        for (auto& e : st) e.Free();
    for (auto& st : m_texUndo)
        for (auto& e : st) e.Free();
    for (auto& st : m_texRedo)
        for (auto& e : st) e.Free();
    for (auto& st : m_undo) st.clear();
    for (auto& st : m_redo) st.clear();
    for (auto& st : m_texUndo) st.clear();
    for (auto& st : m_texRedo) st.clear();
}
