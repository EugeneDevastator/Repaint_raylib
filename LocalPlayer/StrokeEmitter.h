#ifndef LOCALPLAYER_STROKE_EMITTER_H
#define LOCALPLAYER_STROKE_EMITTER_H

#include "brush_draw.h"
#include "repaint.h"
#include "InputQueue.h"
#include "SegmentRenderer.h"

class StrokeEmitter {
public:
    StrokeEmitter(SegmentRenderer* renderer);
    void ProcessInputQueue();

    Vector2 m_lastDabPos;  // public for Distort/Contrast debug
    float m_lastDabRad;

    // Debug
    static const int DBG_SEG_PTS = 2048;
    Vector2 m_segEndpoints[DBG_SEG_PTS];
    int m_segEpCount = 0;
    Vector2 m_splinePts[256];
    int m_splineCount;

private:
    SegmentRenderer* m_renderer;

    bool m_active;
    d_RealBrush m_brushFrom;
    bool m_emittedAny;
    uint16_t m_seed;
    uint8_t m_targetType;
    uint8_t m_targetId;
    uint8_t m_userTexIdx;
    float m_initAngle;
    int   m_toolMode;
    float m_layerScale;

    Vector2 m_prevSegPos, m_prevSegDir;
    float m_prevSegLen;
    float m_initDir;
    bool  m_initDirSet;

    int m_processedCount;
    float m_accumDist;
    Vector2 m_lastInputPos;

    void handleBegin(const InputEntry& e);
    void handlePoint(const InputEntry& e);
    void handleEnd();
    void emitSegment(Vector2 p0, Vector2 p2, Vector2 ctrl0, Vector2 ctrl3,
                     const d_RealBrush& brush, float initAngle, int toolMode);
    void flushSmoothing(const d_RealBrush& brush, float initAngle, int toolMode);
};

extern StrokeEmitter* g_emitter;

#endif
