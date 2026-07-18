#include "StrokeEmitter.h"
#include "StrokeThrottle.h"
#include "stroke_engine.h"
#include "replay_recorder.h"
#include "brush_draw.h"
#include <string.h>
#include <math.h>

StrokeEmitter* g_emitter = nullptr;

StrokeEmitter::StrokeEmitter(StrokeThrottle* throttle)
    : m_throttle(throttle), m_active(false), m_emittedAny(false) {
    m_splineCount = 0;
    m_processedCount = 0;
    m_initDirSet = false;
    m_hasPrevRoot = false;
    m_prevSegLen = 0;
    memset(&m_modulated, 0, sizeof(m_modulated));
    Xform_Identity(m_destXform.mat);
}

void StrokeEmitter::handleBegin(const InputEntry& e) {
    m_active = true;
    m_brushFrom = e.brush;
    CaptureBrushConfig(&m_config);
    m_config.toolMode = e.toolMode;
    m_config.bmidx          = m_brushFrom.bmidx;
    m_config.texColorMode   = m_brushFrom.texColorMode;
    m_config.texNoisemode   = m_brushFrom.texNoisemode;
    m_config.texTiling      = m_brushFrom.texTiling;
    m_config.useTexLumAsAlpha = m_brushFrom.useTexLumAsAlpha;
    m_config.preserveop     = m_brushFrom.preserveop;
    m_config.texBlendMode   = m_brushFrom.texBlendMode;
    m_config.eraseMode      = m_brushFrom.eraseMode;
    m_config.baseSeed       = m_brushFrom.seed;
    m_config.userTexOriginX = m_brushFrom.userTexOriginX;
    m_config.userTexOriginY = m_brushFrom.userTexOriginY;
    m_config.userTexDirection = m_brushFrom.userTexDirection;
    m_emittedAny = false;
    m_seed = e.brush.seed;
    m_initAngle = e.initAngle;
    m_toolMode = e.toolMode;
    m_targetSlot = e.targetSlot;
    m_userTexBucket = e.userTexBucket;
    m_userTexSlot = e.userTexSlot;
    m_worldToTexPx = e.worldToTexPx;
    m_destXform = e.destXform;

    float* m = m_destXform.mat;
    Vector2 start = {e.x * m[0] + e.y * m[1] + m[2],
                     e.x * m[3] + e.y * m[4] + m[5]};
    m_lastDabPos = start;
    m_lastDabRad = 0;
    m_prevSegPos = start;
    m_prevSegDir = Vector2{0, 0};
    m_prevSegLen = 0;
    m_initDirSet = false;
    m_hasPrevRoot = false;
    m_splineCount = 1;
    m_processedCount = 0;
    memset(m_splinePts, 0, sizeof(m_splinePts));
    m_splinePts[0] = start;
    m_segEpCount = 0;
    m_segDebugCount = 0;

    ModulatorTable mt2; Modulator_GetTable(&mt2);
    m_modulated = ResolveModulatedConfig(m_config, e.toolMode, e.initAngle, &mt2);

    // Pixel-perfect: lock radius parity for entire stroke
    m_ppBias = -1.0f;
    if (g_pixelPerfect) {
        float firstRadPx = m_modulated.radOut * e.worldToTexPx;
        int d0 = (int)(firstRadPx * 2.0f + 0.5f);
        if (d0 < 1) d0 = 1;
        m_ppBias = (d0 % 2 == 1) ? 0.5f : 0.0f;
    }

    if (isFirstDabPainted) {
        DabBrush cb = MakeDabBrush(m_modulated, m_brushFrom.rad_out);
        SegmentData dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1 = dseg.pos2 = start;
        dseg.ctrl0 = dseg.ctrl3 = dseg.pos1;
        dseg.brushFrom = dseg.brush = cb;
        dseg.tool     = eSingleStamp;
        dseg.seamless = g_seamlessPaint ? 1 : 0;
        dseg.pixelPerfect = g_pixelPerfect ? 1 : 0;
        dseg.ppBias   = m_ppBias;
        dseg.seed     = e.brush.seed;
        dseg.smudgeSrcX = start.x;
        dseg.smudgeSrcY = start.y;
        dseg.targetSlot = m_targetSlot;
        dseg.userTexBucket = m_userTexBucket;
        dseg.userTexSlot = m_userTexSlot;
        dseg.dabOffset  = 0;
        dseg.initAngle  = e.initAngle;

        m_throttle->Push(dseg);
        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker)   g_broker->on_segment(dseg);

        m_emittedAny = true;
        m_lastDabRad = m_modulated.radOut;
    }
}

void StrokeEmitter::emitSegment(Vector2 p1, Vector2 p2, Vector2 ctrl0, Vector2 ctrl3,
                                const d_RealBrush& brush, float initAngle, int toolMode,
                                const ModulatedBrushConfig& modFrom) {
    float segDx = p2.x - p1.x;
    float segDy = p2.y - p1.y;
    float segLen = sqrtf(segDx*segDx + segDy*segDy);

    // Build local modulator table — tablet/pen values from Modulator,
    // direction/curvature computed locally.
    ModulatorTable mt;
    Modulator_GetTable(&mt);

    // Use exit tangent (p2 - ctrl3) for direction modulation so the
    // segment-end brush rotation anticipates the next segment's entry.
    float exDx = p2.x - ctrl3.x, exDy = p2.y - ctrl3.y;
    float exLen = sqrtf(exDx*exDx + exDy*exDy);
    if (exLen > 0.5f) {
        float exDir = DirAng(exDx, exDy);
        mt.val[csDir] = RngConv(exDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    } else if (segLen > 0.5f) {
        float dirAng = DirAng(segDx, segDy);
        mt.val[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    }
    if (!m_initDirSet && segLen > 0.5f) { m_initDir = DirAng(segDx, segDy); m_initDirSet = true; }
    mt.val[csIdir] = RngConv(m_initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    if (m_prevSegLen > 0.5f && segLen > 0.5f) {
        float dot = (m_prevSegDir.x*segDx + m_prevSegDir.y*segDy) / (m_prevSegLen*segLen);
        mt.val[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
    }
    mt.val[csAcc] = 1.0f;
    if (segLen > 0.001f) mt.val[csHVdir] = fabsf(segDx / segLen);

    m_prevSegPos = p2;
    m_prevSegDir = Vector2{segDx, segDy};
    m_prevSegLen = segLen;

    // Root modulators snapshot for this segment endpoint
    RootModulators root = {};
    root.pressure = mt.val[csPressure];
    root.rotation = mt.val[csRot];
    root.tiltX    = mt.val[csHtilt];
    root.tiltY    = mt.val[csVtilt];
    root.velocity = mt.val[csVel];
    if (segLen > 0.001f) {
        root.dirX = segDx / segLen;
        root.dirY = segDy / segLen;
    }

    ModulatedBrushConfig modTo = ResolveModulatedConfig(m_config, toolMode, initAngle, &mt);
    modTo.radOut *= m_worldToTexPx;
    modTo.jitRadOut *= m_worldToTexPx;

    DabBrush cbFrom = MakeDabBrush(modFrom, m_brushFrom.rad_out);
    DabBrush cbTo   = MakeDabBrush(modTo, modTo.radOut);
    if (!m_emittedAny) {
        // Override modFrom with entry tangent direction so the first
        // segment lerps from entry → exit tangent instead of stale input csDir.
        float c0dx = ctrl0.x - p1.x, c0dy = ctrl0.y - p1.y;
        float c0len = sqrtf(c0dx*c0dx + c0dy*c0dy);
        if (c0len > 0.5f) {
            ModulatorTable mtFrom;
            Modulator_GetTable(&mtFrom);
            mtFrom.val[csDir] = RngConv(DirAng(c0dx, c0dy), -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
            ModulatedBrushConfig modFromFixed = ResolveModulatedConfig(m_config, toolMode, initAngle, &mtFrom);
            cbFrom = MakeDabBrush(modFromFixed, m_brushFrom.rad_out);
        }
        float enDir = (c0len > 0.001f) ? DirAng(c0dx, c0dy) * 180.0f / (float)M_PI : -999.0f;
        float exDir = (exLen > 0.5f) ? DirAng(exDx, exDy) * 180.0f / (float)M_PI : -999.0f;
        float chDir = DirAng(segDx, segDy) * 180.0f / (float)M_PI;
        printf("[SEG1] initAngle=%.1f  chordDir=%.1f  enDir=%.1f  exDir=%.1f  csDir=%.3f  cbFrom.res=%.1f  cbTo.res=%.1f\n",
               initAngle, chDir, enDir, exDir, mt.val[csDir], cbFrom.resangle, cbTo.resangle);
    }

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

    SegmentData dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1      = m_lastDabPos;
    dseg.pos2      = p2;
    dseg.ctrl0     = actualCtrl0;
    dseg.ctrl3     = actualCtrl3;
    dseg.brushFrom = cbFrom;
    dseg.brush     = cbTo;
    dseg.tool      = (uint8_t)toolMode;
    dseg.seamless  = g_seamlessPaint ? 1 : 0;
    dseg.pixelPerfect = g_pixelPerfect ? 1 : 0;
    dseg.ppBias    = m_ppBias;
    dseg.seed      = m_seed;
    dseg.smudgeSrcX = m_lastDabPos.x;
    dseg.smudgeSrcY = m_lastDabPos.y;
    dseg.targetSlot = m_targetSlot;
    dseg.userTexBucket = m_userTexBucket;
    dseg.userTexSlot = m_userTexSlot;
    dseg.dabOffset  = 0;
    dseg.initAngle  = initAngle;
    dseg.fromRoot   = m_hasPrevRoot ? m_prevRoot : root;
    dseg.toRoot     = root;
    m_prevRoot      = root;
    m_hasPrevRoot   = true;
    FillSliderMods(m_config, dseg.sliderMods);

    SegDrawer_SetSegmentStart(m_lastDabRad, m_lastDabPos, &dseg);
    if (!m_emittedAny) dseg.isStrokeStart = 1;

    m_throttle->Push(dseg);

    if (g_recorder) g_recorder->on_segment(dseg);
    if (g_broker) g_broker->on_segment(dseg);

    SegResult segRes;
    int dabCount = DrawLinear(dseg, 0, m_lastDabRad, nullptr, 65536, &segRes);
    m_lastDabPos = segRes.lastDabPos;
    m_lastDabRad = segRes.lastRadOut;
    if (dabCount > 0 && m_segDebugCount < DBG_SEG_PTS / 2) {
        m_segFirstDab[m_segDebugCount] = segRes.firstDabPos;
        m_segLastDab[m_segDebugCount]  = m_lastDabPos;
        m_segEndpoints[m_segDebugCount * 2] = dseg.pos1;
        m_segEndpoints[m_segDebugCount * 2 + 1] = dseg.pos2;
        m_segCtrl0[m_segDebugCount] = dseg.ctrl0;
        m_segCtrl3[m_segDebugCount] = dseg.ctrl3;
        m_segHadDab[m_segDebugCount] = true;
        m_segDebugCount++;
    }
    if (g_pixelPerfect) {
        m_lastDabPos.x = roundf(m_lastDabPos.x);
        m_lastDabPos.y = roundf(m_lastDabPos.y);
    }

    m_modulated = modTo;
    m_emittedAny = true;
}

void StrokeEmitter::handlePoint(const InputEntry& e) {
    if (!m_active) return;

    float* m = m_destXform.mat;
    Vector2 pos = {e.x * m[0] + e.y * m[1] + m[2],
                   e.x * m[3] + e.y * m[4] + m[5]};

    ModulatorTable mt2; Modulator_GetTable(&mt2);
    ModulatedBrushConfig modNow = ResolveModulatedConfig(m_config, m_toolMode, m_initAngle, &mt2);

    if (g_strokeSmoothingMode == SMOOTH_MODE_LINEAR) {
        float lineLen = Dist2D(m_lastDabPos, pos);
        float threshold = (g_strokeThrottle > 0.0f) ? fmaxf(g_strokeThrottle, 0.5f)
                           : modNow.radOut * 0.5f * m_worldToTexPx;
        if (threshold < 0.5f) threshold = 0.5f;
        if (lineLen < threshold) return;
        float hLen = lineLen * 0.33f;
        Vector2 dir = {pos.x - m_lastDabPos.x, pos.y - m_lastDabPos.y};
        if (lineLen > 0.001f) { dir.x /= lineLen; dir.y /= lineLen; }
        Vector2 c0 = {m_lastDabPos.x + dir.x * hLen, m_lastDabPos.y + dir.y * hLen};
        Vector2 c3 = {pos.x - dir.x * hLen, pos.y - dir.y * hLen};
        emitSegment(m_lastDabPos, pos, c0, c3, m_brushFrom, m_initAngle, m_toolMode, m_modulated);
        return;
    }

    float threshold;
    if (g_strokeSmoothingMode == SMOOTH_MODE_SMOOTH) {
        threshold = fmaxf(g_strokeThrottle, 0.5f);
    } else if (g_strokeThrottle <= 0.0f) {
        threshold = modNow.radOut * 0.5f * m_worldToTexPx;
        if (threshold < 0.5f) threshold = 0.5f;
    } else {
        threshold = fmaxf(g_strokeThrottle, 0.5f);
    }

    if (Dist2D(m_splinePts[m_splineCount - 1], pos) >= threshold) {
        if (m_splineCount < 256) {
            m_splinePts[m_splineCount++] = pos;
        } else {
            memmove(m_splinePts, m_splinePts + 1, sizeof(Vector2) * 255);
            m_splinePts[255] = pos;
            if (m_processedCount > 0) m_processedCount--;
        }
    }

    int N = m_splineCount;

    // First real segment: correct modFrom to prevent stale csDir propagation
    if (m_processedCount == 0 && N >= 3) {
        Vector2 firstDx = {m_splinePts[1].x - m_splinePts[0].x,
                           m_splinePts[1].y - m_splinePts[0].y};
        float firstLen = sqrtf(firstDx.x*firstDx.x + firstDx.y*firstDx.y);
        if (firstLen > 0.5f) {
            ModulatorTable ft; Modulator_GetTable(&ft);
            float dirAng = DirAng(firstDx.x, firstDx.y);
            ft.val[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
            ft.val[csIdir] = ft.val[csDir];
            m_modulated = ResolveModulatedConfig(m_config, m_toolMode, m_initAngle, &ft);
        }
    }

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
            emitSegment(p1, p2, c0, c3, m_brushFrom, m_initAngle, m_toolMode, m_modulated);
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
            emitSegment(p1, p2, c0, c3, brush, initAngle, toolMode, m_modulated);
        }
    }
}

void StrokeEmitter::handleEnd() {
    if (!m_active) return;
    flushSmoothing(m_brushFrom, m_initAngle, m_toolMode);

    if (!m_emittedAny) {
        DabBrush cb = MakeDabBrush(m_modulated, m_brushFrom.rad_out);
    SegmentData dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1 = dseg.pos2 = m_lastDabPos;
        dseg.ctrl0 = dseg.ctrl3 = dseg.pos1;
        dseg.brushFrom = dseg.brush = cb;
        dseg.tool = eSingleStamp;
        dseg.seamless = g_seamlessPaint ? 1 : 0;
        dseg.pixelPerfect = g_pixelPerfect ? 1 : 0;
        dseg.ppBias   = m_ppBias;
        dseg.seed = m_seed;
        dseg.smudgeSrcX = m_lastDabPos.x;
        dseg.smudgeSrcY = m_lastDabPos.y;
        dseg.targetSlot = m_targetSlot;
        dseg.userTexBucket = m_userTexBucket;
        dseg.userTexSlot = m_userTexSlot;
        dseg.dabOffset  = 0;
        dseg.initAngle  = m_initAngle;

        m_throttle->Push(dseg);
        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);
    }

    Modulator_ResetStroke();

    m_active = false;
    m_splineCount = 0;
    m_processedCount = 0;
    m_ppBias = -1.0f;
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
