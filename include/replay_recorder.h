#pragma once
#include "raylib.h"
#include "repaint.h"
#include <vector>

struct ReplayRecorder : ICommandBroker {
    std::vector<SegmentData> m_segs;
    int m_canvasW = 512, m_canvasH = 512;
    bool m_playing = false;

    void on_segment(const SegmentData& seg) override;
    void poll(AppState* state) override;

    void Reset(int canvasW, int canvasH);
    bool Save(const char* path);
    bool Load(const char* path);
    void Play(AppState* state);
};
