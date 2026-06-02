#include "StrokeEmitter.h"
#include "stroke_engine.h"
#include "replay_recorder.h"
#include <string.h>
#include <math.h>

StrokeEmitter* g_emitter = nullptr;

StrokeEmitter::StrokeEmitter(SegmentRenderer* renderer)
    : m_renderer(renderer), m_active(false), m_dabIndex(0) {
    m_splineCount = 0;
    m_processedCount = 0;
    m_accumDist = 0;
    m_initDirSet = false;
    m_prevVel = 0;
    m_prevSegLen = 0;
}

// ── handleBegin ────────────────────────────────────────────────────
void StrokeEmitter::handleBegin(const InputEntry& e) {
    m_active = true;
    m_brushFrom = e.brush;
    m_seed = e.brush.seed;
    m_dabIndex = 0;
    m_initAngle = e.initAngle;
    m_toolMode = e.toolMode;
    m_targetType = e.targetType;
    m_targetId = e.targetId;

    Vector2 start = {e.x, e.y};
    m_lastDabPos = start;
    m_lastDabRad = 0;
    m_prevSegPos = start;
    m_prevSegDir = Vector2{0, 0};
    m_prevSegLen = 0;
    m_prevVel = 0;
    m_initDirSet = false;

    m_splineCount = 1;
    m_processedCount = 0;
    m_accumDist = 0;
    m_lastInputPos = start;
    memset(m_splinePts, 0, sizeof(m_splinePts));
    m_splinePts[0] = start;
    m_segEpCount = 0;
}

// ── emitSegment ────────────────────────────────────────────────────
void StrokeEmitter::emitSegment(Vector2 p0, Vector2 p2, Vector2 ctrl0, Vector2 ctrl3,
                               const d_RealBrush& brush, float initAngle, int toolMode) {
    float segDx = p2.x - m_lastDabPos.x;
    float segDy = p2.y - m_lastDabPos.y;
    float segLen = sqrtf(segDx*segDx + segDy*segDy);
    float dirAng = AtanXY(segDx, segDy);

    g_modPars.Pars[csVel] = 0;
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

    CollapsedBrush cbFrom = CollapseBrushParams(m_brushFrom, initAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initAngle, toolMode);

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1      = m_lastDabPos;
    dseg.pos2      = p2;
    dseg.ctrl0     = ctrl0;
    dseg.ctrl3     = ctrl3;
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

    // State tracking — exact lastDabPos for segment chaining
    SegResult r;
    int dabs = DrawLinear(&dseg, m_dabIndex, m_lastDabRad, nullptr, nullptr, 65536, &r);
    m_lastDabPos = r.lastDabPos;
    m_lastDabRad = r.lastRadOut;

    // Debug
    if (m_segEpCount + 1 < DBG_SEG_PTS) {
        m_segEndpoints[m_segEpCount++] = dseg.pos1;
        m_segEndpoints[m_segEpCount++] = dseg.pos2;
    }

    // Push to renderer (resolves textures at draw time)
    m_renderer->Push(dseg);

    // Network send only — not rendering
    if (g_recorder) g_recorder->on_segment(dseg);
    if (g_broker) g_broker->on_segment(dseg);

    m_brushFrom = target;
    m_dabIndex += dabs;
}

// ── handlePoint ────────────────────────────────────────────────────
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
    m_prevVel = e.velocity;

    if (g_strokeSmoothingMode == SMOOTH_MODE_LINEAR) {
        emitSegment(pos, pos, m_lastDabPos, pos, m_brushFrom, m_initAngle, m_toolMode);
        return;
    }

    // Smooth mode
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
        emitSegment(p0, p2, p0, p3, m_brushFrom, m_initAngle, m_toolMode);
        m_processedCount = seg + 1;
    }
}

// ── flushSmoothing ─────────────────────────────────────────────────
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
        emitSegment(p0, p2, p0, p3, brush, initAngle, toolMode);
    }
}

// ── handleEnd ──────────────────────────────────────────────────────
void StrokeEmitter::handleEnd() {
    if (!m_active) return;
    flushSmoothing(m_brushFrom, m_initAngle, m_toolMode);

    if (m_dabIndex == 0) {
        // Single-click stamp
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

        m_renderer->Push(dseg);
        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);
    }

    m_active = false;
    m_splineCount = 0;
    m_processedCount = 0;
}

// ── ProcessInputQueue ──────────────────────────────────────────────
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
