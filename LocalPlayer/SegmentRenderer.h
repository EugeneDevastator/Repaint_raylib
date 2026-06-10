#ifndef LOCALPLAYER_SEGMENT_RENDERER_H
#define LOCALPLAYER_SEGMENT_RENDERER_H

#include "brush_draw.h"
#include "raylib.h"

class DabDrawer;

class SegmentRenderer {
public:
    static const int CAPACITY = 65536;

    void Push(const SegmentData& seg);
    int  EmitPending(int maxSegments, DabDrawer* dd);

private:
    SegmentData m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern SegmentRenderer* g_segRenderer;

#endif
