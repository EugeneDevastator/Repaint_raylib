#pragma once
#include "raylib.h"
#include <deque>

struct AppState;

struct UndoEntry {
    Image snapshot;      // ImageCopy of the layer before the stroke
    int layerIndex;      // which layer this applies to

    // Cleanup helper
    void Free() { if (snapshot.data) UnloadImage(snapshot); snapshot.data = nullptr; }
};

class UndoManager {
public:
    static const int MAX_UNDO = 20;

    void Snapshot(AppState* state, int idx, bool isTexture = false);
    bool Undo(AppState* state, int idx, bool isTexture = false);
    bool Redo(AppState* state, int idx, bool isTexture = false);
    void ClearRedo(int idx, bool isTexture = false);
    void ClearLayer(int idx);
    void InvalidateAll();

    ~UndoManager() { InvalidateAll(); }

private:
    std::deque<UndoEntry> m_undo[256];
    std::deque<UndoEntry> m_redo[256];
    std::deque<UndoEntry> m_texUndo[256];
    std::deque<UndoEntry> m_texRedo[256];
};
