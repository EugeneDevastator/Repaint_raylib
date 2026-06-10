#ifndef LOCALPLAYER_DAB_DRAWER_H
#define LOCALPLAYER_DAB_DRAWER_H

#include "brush_draw.h"

struct AppState;

class DabDrawer {
public:
    static const int CAPACITY = 16384;
    static const int SEGMENT_ESTIMATE = 4096;  // worst-case dabs per segment

    void Push(const DabData& dab);
    bool PushSafe(const DabData& dab);  // returns false if full
    void Clear();
    int  PendingCount();
    int  DrawPending(AppState* state, int pixelBudget);  // resolves RT/tex from state
    bool HasRoom(int need) const;  // true if <need> entries fit

private:
    DabData m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern DabDrawer* g_dabDrawer;

void EmitDabsFromSegment(DabDrawer* dd, const SegmentData& seg, int dabOffset);

#endif
