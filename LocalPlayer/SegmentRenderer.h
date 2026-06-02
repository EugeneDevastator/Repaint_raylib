#ifndef LOCALPLAYER_SEGMENT_RENDERER_H
#define LOCALPLAYER_SEGMENT_RENDERER_H

#include "brush_draw.h"
#include "raylib.h"

struct AppState;

class SegmentRenderer {
public:
    static const int CAPACITY = 65536;

    void Push(const DrawSegment& seg);
    int  RenderPending(AppState* state, int maxPerFrame);

private:
    DrawSegment m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern SegmentRenderer* g_segRenderer;

#endif
