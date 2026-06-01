#include "StrokeEmitter.h"
#include "stroke_engine.h"
#include "repaint.h"
#include "replay_recorder.h"
#include <string.h>

StrokeEmitter* g_emitter = nullptr;

StrokeEmitter::StrokeEmitter(SegmentRenderer* renderer)
    : m_renderer(renderer), m_active(false), m_dabIndex(0) {}

void StrokeEmitter::BeginStroke(float x, float y, const d_RealBrush& brush, float initAngle, int toolMode,
                                uint8_t targetType, uint8_t targetId,
                                RenderTexture2D rt, Texture2D brushTex) {
    m_active = true;
    m_targetType = targetType;
    m_targetId = targetId;
    m_targetRT = rt;
    m_brushTex = brushTex;
    m_initAngle = initAngle;
    m_toolMode = toolMode;
    m_brushFrom = brush;
    m_dabIndex = 0;
    m_lastDabPos = Vector2{x, y};
    m_seed = brush.seed;
    m_prevSegPos = Vector2{x, y};
    m_prevSegDir = Vector2{0, 0};
    m_prevSegLen = 0;
    m_prevVel = 0;
    m_initDirSet = false;
}

void StrokeEmitter::AddPoint(const InputPoint& pt, const d_RealBrush& brush, float initAngle, int toolMode) {
    if (!m_active) return;

    Vector2 pos = {pt.x, pt.y};

    g_modPars.Pars[csPressure] = pt.pressure;
    g_modPars.Pars[csRot]      = pt.rotation;
    g_modPars.Pars[csTilt]     = pt.tiltX;
    g_modPars.Pars[csHtilt]    = pt.tiltX;
    g_modPars.Pars[csVtilt]    = pt.tiltY;
    g_modPars.Pars[csXtilt]    = pt.tiltX;
    g_modPars.Pars[csYtilt]    = pt.tiltY;

    // Stroke geometry modulators (directions, curve, acceleration)
    float segDx = pos.x - m_lastDabPos.x;
    float segDy = pos.y - m_lastDabPos.y;
    float segLen = sqrtf(segDx * segDx + segDy * segDy);
    float dirAng = AtanXY(segDx, segDy);

    g_modPars.Pars[csVel] = pt.velocity;
    g_modPars.Pars[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    if (!m_initDirSet && segLen > 0.5f) { m_initDir = dirAng; m_initDirSet = true; }
    g_modPars.Pars[csIdir] = RngConv(m_initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

    if (m_prevSegLen > 0.5f && segLen > 0.5f) {
        float dot = (m_prevSegDir.x * segDx + m_prevSegDir.y * segDy) / (m_prevSegLen * segLen);
        g_modPars.Pars[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
    }
    g_modPars.Pars[csAcc] = 1.0f - fabsf(pt.velocity - m_prevVel);
    g_modPars.Pars[csAcc] = RngConv(g_modPars.Pars[csAcc], 0.7f, 1.0f, 0.0f, 1.0f);
    if (segLen > 0.001f) g_modPars.Pars[csHVdir] = fabsf(segDx / segLen);

    {
        float dir01 = g_modPars.Pars[csDir], rot01 = brush.resangle / 360.0f;
        float rel = fabsf(dir01 - rot01);
        if (rel > 0.5f) rel = 1.0f - rel;
        rel = rel * 2.0f; rel = 1.0f - fabsf(rel - 0.5f) * 2.0f;
        g_modPars.Pars[csRelang] = rel;
    }

    m_prevSegPos = pos;
    m_prevSegDir = Vector2{segDx, segDy};
    m_prevSegLen = segLen;
    m_prevVel = pt.velocity;

    // Brush modulation with geometry-aware modulators
    d_RealBrush target = brush;
    float sizeMul = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    target.rad_out  = GetModVal(&bpSize);
    target.radInRatio = GetModVal(&bpHardness);
    target.crv      = GetModVal(&bpCurvature);
    target.opacity  = GetModVal(&bpOpacity);
    target.resangle = fmodf(initAngle + GetModVal(&bpAngle), 360.0f);
    target.x2y      = GetModVal(&bpScaleRel);
    target.col      = HSLToRGB(GetModVal(&bpQuickHue), GetModVal(&bpQuickSat), GetModVal(&bpQuickLit));
    target.cop      = (toolMode == eSmudge) ? GetModVal(&bpCloneOpacity) : 0.0f;
    target.rad_out *= sizeMul;

    CollapsedBrush cbFrom = CollapseBrushParams(m_brushFrom, initAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initAngle, toolMode);

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1      = m_lastDabPos;
    dseg.pos2      = pos;
    dseg.ctrl0     = m_lastDabPos;
    dseg.ctrl3     = pos;
    dseg.brushFrom = cbFrom;
    dseg.brush     = cbTo;
    dseg.Noisemode = 0;
    dseg.tool      = (uint8_t)toolMode;
    dseg.seamless  = g_seamlessPaint ? 1 : 0;
    dseg.seed      = m_seed;
    dseg.smudgeSrcX = m_lastDabPos.x;
    dseg.smudgeSrcY = m_lastDabPos.y;
    dseg.targetType = m_targetType;
    dseg.targetId   = m_targetId;

    // Compute exact last dab position for chaining (with radius continuity)
    SegResult r;
    int dabs = DrawLinear(&dseg, m_dabIndex, 0.0f, nullptr, nullptr, 65536, &r);
    m_lastDabPos = r.lastDabPos;

    // Push to segment renderer for local drawing
    PendingDraw pd;
    pd.seg = dseg;
    pd.targetRT = m_targetRT;
    pd.brushTex = m_brushTex;
    pd.seamless = dseg.seamless != 0;
    m_renderer->Push(pd);

    // Network / recording
    if (g_recorder) g_recorder->on_segment(dseg);
    if (g_broker) g_broker->on_segment(dseg);

    m_brushFrom = target;
    m_dabIndex += dabs;
}

void StrokeEmitter::EndStroke() {
    if (!m_active) return;

    if (m_dabIndex == 0) {
        // Single click stamp
        CollapsedBrush cb = CollapseBrushParams(m_brushFrom, m_initAngle, m_toolMode);
        DrawSegment dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1 = dseg.pos2 = m_lastDabPos;
        dseg.ctrl0 = dseg.ctrl3 = dseg.pos1;
        dseg.brushFrom = dseg.brush = cb;
        dseg.tool = eSingleStamp;
        dseg.seamless = g_seamlessPaint ? 1 : 0;
        dseg.seed = m_seed;
        dseg.smudgeSrcX = m_lastDabPos.x;
        dseg.smudgeSrcY = m_lastDabPos.y;
        dseg.targetType = m_targetType;
        dseg.targetId   = m_targetId;

        PendingDraw pd;
        pd.seg = dseg;
        pd.targetRT = m_targetRT;
        pd.brushTex = m_brushTex;
        pd.seamless = dseg.seamless != 0;
        m_renderer->Push(pd);

        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);
    }

    m_active = false;
}
