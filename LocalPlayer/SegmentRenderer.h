#ifndef LOCALPLAYER_SEGMENT_RENDERER_H
#define LOCALPLAYER_SEGMENT_RENDERER_H

#include "brush_draw.h"
#include "raylib.h"

struct PendingDraw {
    DrawSegment seg;
    RenderTexture2D targetRT;
    Texture2D brushTex;
    bool seamless;
};

class SegmentRenderer {
public:
    static const int CAPACITY = 65536;

    void Push(const PendingDraw& d);
    int  RenderPending(int maxPerFrame);

private:
    PendingDraw m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern SegmentRenderer* g_segRenderer;

#endif
