#ifndef STROKE_THROTTLE_H
#define STROKE_THROTTLE_H

#include "brush_draw.h"

struct AppState;

class StrokeThrottle {
public:
    static const int SEG_CAP = 65536;
    static const int DAB_CAP = 65536;

    void Push(const SegmentData& seg);
    int  DrawPending(AppState* state);

private:
    SegmentData m_segQ[SEG_CAP];
    int m_segHead = 0, m_segTail = 0;

    DabPoint m_dabBuf[DAB_CAP];
    int m_dabCount = 0;
    int m_dabIdx = 0;

    TexSlotID m_targetSlot;
    uint8_t m_userTexBucket, m_userTexSlot;
    bool m_seamless, m_pixelPerfect;

    float m_lastSegEndAngle = 0.0f;
    bool  m_hasPrevAngle = false;
    float m_prevSmudgeSrcX = 0.0f, m_prevSmudgeSrcY = 0.0f;
    bool  m_hasPrevSmudge = false;
    int m_dynamicBudget = 2 * 1024 * 1024;
    int m_minBudget = 10000;
    int m_maxBudget = 8 * 1024 * 1024;
    double m_targetFrameTime = 1.0 / 75.0;
};

extern StrokeThrottle* g_throttle;

#endif
