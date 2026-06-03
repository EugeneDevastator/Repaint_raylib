#ifndef LOCALPLAYER_DAB_DRAWER_H
#define LOCALPLAYER_DAB_DRAWER_H

#include "brush_draw.h"

struct DabEntry {
    float x, y, srcX, srcY;
    RenderTexture2D rt;
    CollapsedBrush brush;
    Texture2D brushTex;
    bool seamless;
};

class DabDrawer {
public:
    static const int CAPACITY = 16384;
    static const int SEGMENT_ESTIMATE = 4096;  // worst-case dabs per segment

    void Push(const DabEntry& dab);
    bool PushSafe(const DabEntry& dab);  // returns false if full
    void Clear();
    int  PendingCount();
    int  DrawPending(int maxPerFrame);
    bool HasRoom(int need) const;  // true if <need> entries fit

private:
    DabEntry m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern DabDrawer* g_dabDrawer;

void EmitDabsFromSegment(DabDrawer* dd, const DrawSegment& seg,
    RenderTexture2D rt, Texture2D brushTex, bool seamless, int dabOffset);

#endif
