#include "StrokeEmitter.h"
#include "stroke_engine.h"
#include "replay_recorder.h"
#include "brush_draw.h"
#include <string.h>
#include <math.h>

StrokeEmitter* g_emitter = nullptr;

StrokeEmitter::StrokeEmitter(SegmentRenderer* renderer)
    : m_renderer(renderer), m_active(false), m_emittedAny(false) {
    m_splineCount = 0;
    m_processedCount = 0;
    m_accumDist = 0;
    m_initDirSet = false;
    m_prevSegLen = 0;
}

void StrokeEmitter::handleBegin(const InputEntry& e) {
    m_active = true;
    m_brushFrom = e.brush;
    m_emittedAny = false;
    m_seed = e.brush.seed;
    m_initAngle = e.initAngle;
    m_toolMode = e.toolMode;
    m_targetType = e.targetType;
    m_targetId = e.targetId;
    m_layerScale = e.layerScale;

    Vector2 start = {e.x, e.y};
    m_lastDabPos = start;
    m_lastDabRad = 0;
    m_prevSegPos = start;
    m_prevSegDir = Vector2{0, 0};
    m_prevSegLen = 0;
    g_modPars.Pars[csVel] = 0.0f;
    m_initDirSet = false;
    m_splineCount = 1;
    m_processedCount = 0;
    m_accumDist = 0;
    m_lastInputPos = start;
    memset(m_splinePts, 0, sizeof(m_splinePts));
    m_splinePts[0] = start;
    m_segEpCount = 0;
}

void StrokeEmitter::emitSegment(Vector2 p1, Vector2 p2, Vector2 ctrl0, Vector2 ctrl3,
                               const d_RealBrush& brush, float initAngle, int toolMode) {
    float segDx = p2.x - m_lastDabPos.x;
    float segDy = p2.y - m_lastDabPos.y;
    float segLen = sqrtf(segDx*segDx + segDy*segDy);
    float dirAng = AtanXY(segDx, segDy);

    g_modPars.Pars[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    if (!m_initDirSet && segLen > 0.5f) { m_initDir = dirAng; m_initDirSet = true; }
    g_modPars.Pars[csIdir] = RngConv(m_initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    if (m_prevSegLen > 0.5f && segLen > 0.5f) {
        float dot = (m_prevSegDir.x*segDx + m_prevSegDir.y*segDy) / (m_prevSegLen*segLen);
        g_modPars.Pars[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
    }
    g_modPars.Pars[csAcc] = 1.0f;
    if (segLen > 0.001f) g_modPars.Pars[csHVdir] = fabsf(segDx / segLen);
    m_prevSegPos = p2;
    m_prevSegDir = Vector2{segDx, segDy};
    m_prevSegLen = segLen;

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
    if (m_layerScale > 0.001f && fabsf(m_layerScale - 1.0f) > 0.0001f)
        target.rad_out *= m_layerScale;

    CollapsedBrush cbFrom = CollapseBrushParams(m_brushFrom, initAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initAngle, toolMode);

    // Rebase ctrl0 from p1 to m_lastDabPos, and ctrl3 from p2 to the actual
    // segment end, rescaling both handle lengths to the actual segment length.
    float hLen = segLen * 0.33f;
    Vector2 c0dir = {ctrl0.x - p1.x, ctrl0.y - p1.y};
    float c0l = sqrtf(c0dir.x*c0dir.x + c0dir.y*c0dir.y);
    Vector2 actualCtrl0 = m_lastDabPos;
    if (c0l > 0.001f) {
        actualCtrl0.x = m_lastDabPos.x + c0dir.x/c0l * hLen;
        actualCtrl0.y = m_lastDabPos.y + c0dir.y/c0l * hLen;
    }
    Vector2 c3dir = {ctrl3.x - p2.x, ctrl3.y - p2.y};
    float c3l = sqrtf(c3dir.x*c3dir.x + c3dir.y*c3dir.y);
    Vector2 actualCtrl3 = p2;
    if (c3l > 0.001f) {
        actualCtrl3.x = p2.x + c3dir.x/c3l * hLen;
        actualCtrl3.y = p2.y + c3dir.y/c3l * hLen;
    }

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1      = m_lastDabPos;
    dseg.pos2      = p2;
    dseg.ctrl0     = actualCtrl0;
    dseg.ctrl3     = actualCtrl3;
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
    dseg.dabOffset  = 0;

    SegDrawer_SetSegmentStart(m_lastDabRad, m_lastDabPos, &dseg);

    if (m_segEpCount + 1 < DBG_SEG_PTS) {
        m_segEndpoints[m_segEpCount++] = dseg.pos1;
        m_segEndpoints[m_segEpCount++] = dseg.pos2;
    }

    m_renderer->Push(dseg);

    if (g_recorder) g_recorder->on_segment(dseg);
    if (g_broker) g_broker->on_segment(dseg);

    SegDrawer_ComputeSegmentEnd(&dseg, 0, m_lastDabRad, &m_lastDabPos, &m_lastDabRad);

    m_brushFrom = target;
    m_emittedAny = true;
}

void StrokeEmitter::handlePoint(const InputEntry& e) {
    if (!m_active) return;

    Vector2 pos = {e.x, e.y};
    g_modPars.Pars[csPressure] = e.pressure;
    g_modPars.Pars[csRot]      = e.rotation;
    g_modPars.Pars[csTilt]     = e.tiltX;
    g_modPars.Pars[csHtilt]    = e.tiltX;
    g_modPars.Pars[csVtilt]    = e.tiltY;
    g_modPars.Pars[csXtilt]    = e.tiltX;
    g_modPars.Pars[csYtilt]    = e.tiltY;

    // Velocity from input filter (already smoothed in Feed())
    g_modPars.Pars[csVel] = e.velocity;

    if (g_strokeSmoothingMode == SMOOTH_MODE_LINEAR) {
        float lineLen = Dist2D(m_lastDabPos, pos);
        float hLen = lineLen * 0.33f;
        Vector2 dir = {pos.x - m_lastDabPos.x, pos.y - m_lastDabPos.y};
        if (lineLen > 0.001f) { dir.x /= lineLen; dir.y /= lineLen; }
        Vector2 c0 = {m_lastDabPos.x + dir.x * hLen, m_lastDabPos.y + dir.y * hLen};
        Vector2 c3 = {pos.x - dir.x * hLen, pos.y - dir.y * hLen};
        emitSegment(m_lastDabPos, pos, c0, c3, m_brushFrom, m_initAngle, m_toolMode);
        return;
    }

    float threshold = fmaxf(g_strokeThrottle, 0.5f);
    if (m_splineCount < 4) threshold = fminf(threshold, 5.0f);

    float dist = Dist2D(m_lastInputPos, pos);
    m_accumDist += dist;
    m_lastInputPos = pos;

    if (m_accumDist >= threshold) {
        if (m_splineCount < 256) {
            m_splinePts[m_splineCount++] = pos;
        } else {
            memmove(m_splinePts, m_splinePts + 1, sizeof(Vector2) * 255);
            m_splinePts[255] = pos;
            if (m_processedCount > 0) m_processedCount--;
            m_splinePts[m_processedCount] = m_lastDabPos;
        }
        m_accumDist = 0;
    }

    int N = m_splineCount;
    for (int seg = m_processedCount; seg <= N - 3; seg++) {
        Vector2 p0, p1, p2, p3;
        if (seg == 0) {
            p0 = m_splinePts[0]; p1 = m_splinePts[0];
            p2 = m_splinePts[1]; p3 = m_splinePts[2];
        } else {
            p0 = m_splinePts[seg - 1];
            p1 = m_splinePts[seg];
            p2 = m_splinePts[seg + 1];
            p3 = m_splinePts[seg + 2];
        }
        float segLen = Dist2D(p1, p2);
        if (segLen < 0.5f) { m_processedCount = seg + 1; continue; }
        {
            float hLen = segLen * 0.33f;
            Vector2 t1 = {p2.x - p0.x, p2.y - p0.y};
            float t1l = sqrtf(t1.x*t1.x + t1.y*t1.y);
            Vector2 t2 = {p3.x - p1.x, p3.y - p1.y};
            float t2l = sqrtf(t2.x*t2.x + t2.y*t2.y);
            Vector2 c0 = p1, c3 = p2;
            if (t1l > 0.001f) { c0.x = p1.x + t1.x/t1l * hLen; c0.y = p1.y + t1.y/t1l * hLen; }
            if (t2l > 0.001f) { c3.x = p2.x - t2.x/t2l * hLen; c3.y = p2.y - t2.y/t2l * hLen; }
            emitSegment(p1, p2, c0, c3, m_brushFrom, m_initAngle, m_toolMode);
        }
        m_processedCount = seg + 1;
    }
}

void StrokeEmitter::flushSmoothing(const d_RealBrush& brush, float initAngle, int toolMode) {
    if (g_strokeSmoothingMode != SMOOTH_MODE_SMOOTH) return;
    int N = m_splineCount;
    for (int seg = m_processedCount; seg <= N - 2; seg++) {
        Vector2 p0, p1, p2, p3;
        if (seg == 0) {
            p0 = m_splinePts[0]; p1 = m_splinePts[0];
            p2 = m_splinePts[1]; p3 = (N > 2) ? m_splinePts[2] : m_splinePts[1];
        } else if (seg >= N - 2) {
            p0 = m_splinePts[seg - 1];
            p1 = m_splinePts[seg];
            p2 = m_splinePts[seg + 1];
            p3 = m_splinePts[seg + 1];
        } else {
            p0 = m_splinePts[seg - 1];
            p1 = m_splinePts[seg];
            p2 = m_splinePts[seg + 1];
            p3 = m_splinePts[seg + 2];
        }
        float segLen = Dist2D(p1, p2);
        if (segLen < 0.5f) continue;
        {
            float hLen = segLen * 0.33f;
            Vector2 t1 = {p2.x - p0.x, p2.y - p0.y};
            float t1l = sqrtf(t1.x*t1.x + t1.y*t1.y);
            Vector2 t2 = {p3.x - p1.x, p3.y - p1.y};
            float t2l = sqrtf(t2.x*t2.x + t2.y*t2.y);
            Vector2 c0 = p1, c3 = p2;
            if (t1l > 0.001f) { c0.x = p1.x + t1.x/t1l * hLen; c0.y = p1.y + t1.y/t1l * hLen; }
            if (t2l > 0.001f) { c3.x = p2.x - t2.x/t2l * hLen; c3.y = p2.y - t2.y/t2l * hLen; }
            emitSegment(p1, p2, c0, c3, brush, initAngle, toolMode);
        }
    }
}

void StrokeEmitter::handleEnd() {
    if (!m_active) return;
    flushSmoothing(m_brushFrom, m_initAngle, m_toolMode);

    if (!m_emittedAny) {
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
        dseg.dabOffset  = 0;

        m_renderer->Push(dseg);
        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);
    }

    m_active = false;
    m_splineCount = 0;
    m_processedCount = 0;
}

void StrokeEmitter::ProcessInputQueue() {
    InputEntry entries[256];
    int n = g_inputQueue.Drain(entries, 256);
    for (int i = 0; i < n; i++) {
        switch (entries[i].type) {
        case InputEntry::Begin:  handleBegin(entries[i]); break;
        case InputEntry::Point:  handlePoint(entries[i]); break;
        case InputEntry::End:    handleEnd(); break;
        }
    }
}
