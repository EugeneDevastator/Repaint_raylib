#include "undo.h"
#include "repaint.h"
#include "layerstack.h"

void UndoManager::Snapshot(AppState* state, int layerIdx) {
    if (layerIdx < 0 || layerIdx >= LayerStack_Count()) return;

    LayerStack_SyncImageFromRT(layerIdx);
    Image* src = LayerStack_GetImage(layerIdx);
    if (!src || !src->data) return;

    UndoEntry entry;
    entry.snapshot = ImageCopy(*src);
    entry.layerIndex = layerIdx;

    auto& st = m_undo[layerIdx];
    st.push_back(std::move(entry));
    while ((int)st.size() > MAX_UNDO) {
        st.front().Free();
        st.pop_front();
    }
}

bool UndoManager::Undo(AppState* state, int layerIdx) {
    if (layerIdx < 0 || layerIdx >= LayerStack_Count()) return false;
    auto& st = m_undo[layerIdx];
    if (st.empty()) return false;

    LayerStack_SyncImageFromRT(layerIdx);
    Image* cur = LayerStack_GetImage(layerIdx);
    if (!cur || !cur->data) return false;

    // Save current state for redo
    UndoEntry redo;
    redo.snapshot = ImageCopy(*cur);
    redo.layerIndex = layerIdx;
    m_redo[layerIdx].push_back(std::move(redo));

    // Restore undo snapshot (swap images)
    UndoEntry& undo = st.back();
    UnloadImage(*cur);
    *cur = undo.snapshot;
    undo.snapshot.data = nullptr;
    undo.snapshot = {0};
    LayerStack_SyncRTFromImage(layerIdx);

    st.pop_back();
    return true;
}

bool UndoManager::Redo(AppState* state, int layerIdx) {
    if (layerIdx < 0 || layerIdx >= LayerStack_Count()) return false;
    auto& st = m_redo[layerIdx];
    if (st.empty()) return false;

    LayerStack_SyncImageFromRT(layerIdx);
    Image* cur = LayerStack_GetImage(layerIdx);
    if (!cur || !cur->data) return false;

    // Save current for undo (allows undo after redo)
    UndoEntry reundo;
    reundo.snapshot = ImageCopy(*cur);
    reundo.layerIndex = layerIdx;
    m_undo[layerIdx].push_back(std::move(reundo));

    // Restore redo snapshot
    UndoEntry& redo = st.back();
    UnloadImage(*cur);
    *cur = redo.snapshot;
    redo.snapshot.data = nullptr;
    redo.snapshot = {0};
    LayerStack_SyncRTFromImage(layerIdx);

    st.pop_back();
    return true;
}

void UndoManager::ClearRedo(int layerIdx) {
    if (layerIdx < 0 || layerIdx >= 256) return;
    for (auto& e : m_redo[layerIdx]) e.Free();
    m_redo[layerIdx].clear();
}

void UndoManager::ClearLayer(int layerIdx) {
    if (layerIdx < 0 || layerIdx >= 256) return;
    for (auto& e : m_undo[layerIdx]) e.Free();
    m_undo[layerIdx].clear();
    for (auto& e : m_redo[layerIdx]) e.Free();
    m_redo[layerIdx].clear();
}

void UndoManager::InvalidateAll() {
    for (auto& st : m_undo)
        for (auto& e : st) e.Free();
    for (auto& st : m_redo)
        for (auto& e : st) e.Free();
    for (auto& st : m_undo) st.clear();
    for (auto& st : m_redo) st.clear();
}
