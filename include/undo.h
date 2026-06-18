#pragma once
#include "raylib.h"
#include "texture_manager.h"
#include <deque>

struct AppState;

struct UndoEntry {
    Image snapshot;

    void Free() { if (snapshot.data) UnloadImage(snapshot); snapshot.data = nullptr; }
};

class UndoManager {
public:
    static const int MAX_UNDO = 20;
    static const int MAX_SLOTS = TM_BUCKETS * TM_SLOTS_PER_BUCKET;

    void Snapshot(AppState* state, TexSlotID slot);
    bool Undo(AppState* state, TexSlotID slot);
    bool Redo(AppState* state, TexSlotID slot);
    void InvalidateAll();

    ~UndoManager() { InvalidateAll(); }

private:
    int flatIdx(TexSlotID id) const { return id.bucket * TM_SLOTS_PER_BUCKET + id.slot; }

    std::deque<UndoEntry> m_undo[MAX_SLOTS];
    std::deque<UndoEntry> m_redo[MAX_SLOTS];
};
