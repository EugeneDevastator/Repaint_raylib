#ifndef LOCALPLAYER_STROKE_EMITTER_H
#define LOCALPLAYER_STROKE_EMITTER_H

#include "brush_draw.h"
#include "repaint.h"
#include "InputQueue.h"

class StrokeThrottle;

class StrokeEmitter {
public:
    StrokeEmitter(StrokeThrottle* throttle);
    void ProcessInputQueue();

    Vector2 m_lastDabPos;  // public for Distort/Contrast debug
    float m_lastDabRad;
    bool isFirstDabPainted = true;

    // Debug
    static const int DBG_SEG_PTS = 2048;
    Vector2 m_segEndpoints[DBG_SEG_PTS];
    int m_segEpCount = 0;
    Vector2 m_splinePts[256];
    int m_splineCount;

private:
    StrokeThrottle* m_throttle;

    bool m_active;
    d_RealBrush m_brushFrom;
    UserBrushConfig m_config;
    bool m_emittedAny;
    uint16_t m_seed;
    TexSlotID m_targetSlot;
    uint8_t m_userTexBucket;
    uint8_t m_userTexSlot;
    float m_initAngle;
    int   m_toolMode;
    float m_worldToTexPx;

    Vector2 m_prevSegPos, m_prevSegDir;
    float m_prevSegLen;
    float m_initDir;
    bool  m_initDirSet;

    int m_processedCount;

    void handleBegin(const InputEntry& e);
    void handlePoint(const InputEntry& e);
    void handleEnd();
    void emitSegment(Vector2 p0, Vector2 p2, Vector2 ctrl0, Vector2 ctrl3,
                     const d_RealBrush& brush, float initAngle, int toolMode);
    void flushSmoothing(const d_RealBrush& brush, float initAngle, int toolMode);
};

extern StrokeEmitter* g_emitter;

#endif
