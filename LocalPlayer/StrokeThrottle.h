#ifndef STROKE_THROTTLE_H
#define STROKE_THROTTLE_H

#include "brush_draw.h"

struct AppState;

class StrokeThrottle {
public:
    static const int SEG_CAP = 65536;
    static const int DAB_CAP = 65536;

    void Push(const SegmentData& seg);
    int  DrawPending(AppState* state, int pixelBudget);

private:
    SegmentData m_segQ[SEG_CAP];
    int m_segHead = 0, m_segTail = 0;

    DabPoint m_dabBuf[DAB_CAP];
    int m_dabCount = 0;
    int m_dabIdx = 0;

    // Cached segment metadata for current dab batch
    TexSlotID m_targetSlot;
    uint8_t m_userTexBucket, m_userTexSlot;
    bool m_seamless, m_pixelPerfect;

    // Angle tracking across segments: forward last dab's angle so
    // the first dab of the next segment gets a correct angle delta.
    float m_lastSegEndAngle = 0.0f;
    bool  m_hasPrevAngle = false;
};

extern StrokeThrottle* g_throttle;

#endif
