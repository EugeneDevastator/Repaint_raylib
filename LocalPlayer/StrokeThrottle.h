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
    uint8_t m_targetType, m_targetId, m_userTexIdx;
    bool m_seamless, m_pixelPerfect;
};

extern StrokeThrottle* g_throttle;

#endif
