#ifndef LOCALPLAYER_STROKE_EMITTER_H
#define LOCALPLAYER_STROKE_EMITTER_H

#include "brush_draw.h"
#include "repaint.h"
#include "tablet.h"
#include "InputQueue.h"
#include "SegmentRenderer.h"

class StrokeEmitter {
public:
    StrokeEmitter(SegmentRenderer* renderer);

    void BeginStroke(float x, float y, const d_RealBrush& brush, float initAngle, int toolMode,
                     uint8_t targetType, uint8_t targetId,
                     RenderTexture2D rt, Texture2D brushTex);
    void AddPoint(const InputPoint& pt, const d_RealBrush& brush, float initAngle, int toolMode);
    void EndStroke();

    Vector2 m_lastDabPos;
    int dabCount() const { return m_dabIndex; }

private:
    SegmentRenderer* m_renderer;
    bool m_active;
    d_RealBrush m_brushFrom;
    int  m_dabIndex;
    uint8_t m_targetType, m_targetId;
    RenderTexture2D m_targetRT;
    Texture2D m_brushTex;
    float m_initAngle;
    int   m_toolMode;
    uint16_t m_seed;

    Vector2 m_prevSegPos, m_prevSegDir;
    float m_prevSegLen, m_prevVel;
    float m_initDir;
    bool  m_initDirSet;
};

extern StrokeEmitter* g_emitter;

#endif
