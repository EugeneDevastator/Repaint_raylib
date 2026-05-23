#include "repaint.h"
#include "stroke_engine.h"
#include "stroke.h"
#include <math.h>

int g_strokeSmoothingMode = SMOOTH_MODE_LINEAR;
float g_splineMinDist = 0.0f;
float g_splineAngleThreshold = 60.0f;

// ── BaseModVal helper ────────────────────────────────────────────────
static inline float BaseModVal(const BParam& bp, float cpar) {
    float rng = bp.run.clipmaxF - bp.run.clipminF;
    float base = cpar * rng + bp.run.clipminF;
    base = fminf(fmaxf(base, 0.0f), 1.0f);
    return base * (bp.outMax - bp.outMin) + bp.outMin;
}

// ── Bridge: collapse UI brush → drawing-space brush ─────────────────
CollapsedBrush CollapseBrushParams(const d_RealBrush& b, float initialAngle, int toolMode) {
    CollapsedBrush cb;
    cb.rad_out_px = b.rad_out;
    cb.radInRatio = fminf(b.radInRatio, 1.0f);
    cb.scale_x    = 1.0f;
    cb.scale_y    = b.x2y;
    cb.resangle   = (float)b.resangle;
    cb.opacity    = b.opacity;
    cb.crv        = b.crv;
    cb.cop        = (toolMode == eSmudge) ? b.cop : 0.0f;
    cb.col        = b.col;
    cb.pwr        = b.pwr;
    cb.bmidx      = (int)b.bmidx;
    cb.eraseMode  = b.eraseMode;
    cb.preserveop = b.preserveop;
    cb.perspective = b.perspective;
    cb.texScale   = b.texScale;
    cb.texFeather = b.texFeather;
    cb.texThresh  = b.texThresh;
    cb.texBlendVal = b.texBlendVal;
    cb.texBlendMode = b.texBlendMode;
    cb.texNoisemode = b.texNoisemode;
    cb.texColorMode = b.texColorMode;
    cb.useTexLumAsAlpha = b.useTexLumAsAlpha;

    // Jitter ranges: proportional to final values in drawing space
    cb.jitRadOut  = bpSize.user.jitter * b.rad_out;
    cb.jitRadIn   = bpHardness.user.jitter;
    cb.jitOpacity = bpOpacity.user.jitter;
    cb.jitCrv     = bpCurvature.user.jitter;
    cb.jitX2y     = bpScaleRel.user.jitter;
    cb.jitHue     = bpQuickHue.user.jitter * (bpQuickHue.outMax - bpQuickHue.outMin);
    cb.jitSat     = bpQuickSat.user.jitter * (bpQuickSat.outMax - bpQuickSat.outMin);
    cb.jitLit     = bpQuickLit.user.jitter * (bpQuickLit.outMax - bpQuickLit.outMin);
    cb.jitCloneOp = bpCloneOpacity.user.jitter;
    cb.baseSeed   = b.seed;
    return cb;
}

// ── Catmull-Rom spline ──────────────────────────────────────────────
static Vector2 CatmullRom(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return Vector2{
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)
    };
}

// ── StrokeEngine implementation ─────────────────────────────────────

void StrokeEngine_Init(StrokeEngine* se) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->lastDabPos = Vector2{0, 0};
    se->smudgeSrcPos = Vector2{0, 0};
    se->inStroke = false;
    se->prevSegPos = Vector2{0, 0};
    se->prevSegDir = Vector2{0, 0};
    se->prevSegLen = 0.0f;
    se->prevVel = 0.0f;
    se->initDir = 0.0f;
    se->initDirSet = false;
    se->splineCount = 0;
    se->processedCount = 0;
    memset(se->splinePts, 0, sizeof(se->splinePts));
    se->smoothBufCount = 0;
    se->dabIndex = 0;
    se->lastDabRad = 0.0f;
}

void StrokeEngine_BeginStroke(StrokeEngine* se, const d_Brush* baseBrush, float x, float y) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->segBrushFrom = *baseBrush;
    se->lastDabPos = Vector2{x, y};
    se->smudgeSrcPos = Vector2{x, y};
    se->inStroke = true;
    se->prevSegPos = Vector2{x, y};
    se->prevSegDir = Vector2{0, 0};
    se->prevSegLen = 0.0f;
    se->prevVel = 0.0f;
    se->initDirSet = false;
    se->splineCount = 1;
    se->processedCount = 0;
    memset(se->splinePts, 0, sizeof(se->splinePts));
    se->splinePts[0] = Vector2{x, y};
    se->dabIndex = 0;
    se->lastDabRad = 0.0f;
}

static int FeedOnePoint(StrokeEngine* se, Vector2 pos, float velocity,
                        const d_RealBrush* baseBrush,
                        float initialAngle, int toolMode,
                        DrawDab* outDabs, int maxDabs) {
    if (!se->inStroke || maxDabs <= 0) return 0;

    float segDx = pos.x - se->prevSegPos.x;
    float segDy = pos.y - se->prevSegPos.y;
    float segLen = sqrtf(segDx * segDx + segDy * segDy);
    float dirAng = AtanXY(segDx, segDy);

    g_modPars.Pars[csVel] = velocity;
    g_modPars.Pars[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    if (!se->initDirSet && segLen > 0.5f) { se->initDir = dirAng; se->initDirSet = true; }
    g_modPars.Pars[csIdir] = RngConv(se->initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

    if (se->prevSegLen > 0.5f && segLen > 0.5f) {
        float dot = (se->prevSegDir.x * segDx + se->prevSegDir.y * segDy) / (se->prevSegLen * segLen);
        g_modPars.Pars[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
    }
    g_modPars.Pars[csAcc] = 1.0f - fabsf(velocity - se->prevVel);
    g_modPars.Pars[csAcc] = RngConv(g_modPars.Pars[csAcc], 0.7f, 1.0f, 0.0f, 1.0f);
    if (segLen > 0.001f) g_modPars.Pars[csHVdir] = fabsf(segDx / segLen);

    {
        float dir01 = g_modPars.Pars[csDir], rot01 = baseBrush->resangle / 360.0f;
        float rel = fabsf(dir01 - rot01);
        if (rel > 0.5f) rel = 1.0f - rel;
        rel = rel * 2.0f; rel = 1.0f - fabsf(rel - 0.5f) * 2.0f;
        g_modPars.Pars[csRelang] = rel;
    }

    se->prevSegPos = pos;
    se->prevSegDir = Vector2{segDx, segDy};
    se->prevSegLen = segLen;
    se->prevVel = velocity;

    // Build modulated brush
    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);

    d_RealBrush target = *baseBrush;
    target.rad_out  = BaseModVal(bpSize,       g_modPars.Pars[bpSize.penMode]);
    float hVal      = BaseModVal(bpHardness,   g_modPars.Pars[bpHardness.penMode]);
    target.radInRatio = hVal;
    target.crv      = BaseModVal(bpCurvature,  g_modPars.Pars[bpCurvature.penMode]);
    target.opacity  = BaseModVal(bpOpacity,    g_modPars.Pars[bpOpacity.penMode]);
    target.resangle = fmodf(initialAngle + BaseModVal(bpAngle, g_modPars.Pars[bpAngle.penMode]), 360.0f);
    target.x2y      = BaseModVal(bpScaleRel,   g_modPars.Pars[bpScaleRel.penMode]);
    target.col      = HSLToRGB(BaseModVal(bpQuickHue, g_modPars.Pars[bpQuickHue.penMode]),
                               BaseModVal(bpQuickSat, g_modPars.Pars[bpQuickSat.penMode]),
                               BaseModVal(bpQuickLit, g_modPars.Pars[bpQuickLit.penMode]));
    target.cop = (toolMode == eSmudge) ? BaseModVal(bpCloneOpacity, g_modPars.Pars[bpCloneOpacity.penMode]) : 0.0f;
    target.rad_out *= sizeMulFactor;

    float spacingVal = BParam_GetValue(&bpSpacing);

    // Collapse both endpoint brushes
    CollapsedBrush cbFrom = CollapseBrushParams(se->segBrushFrom.Realb, initialAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initialAngle, toolMode);

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1     = se->lastDabPos;
    dseg.pos2     = pos;
    if (g_strokeSmoothingMode == SMOOTH_MODE_SMOOTH && se->smoothBufCount >= 4) {
        dseg.ctrl0 = se->smoothBuf[se->smoothBufCount - 4];
        dseg.ctrl3 = se->smoothBuf[se->smoothBufCount - 1];
    } else {
        dseg.ctrl0 = se->lastDabPos;
        dseg.ctrl3 = pos;
    }
    dseg.brushFrom = cbFrom;
    dseg.brush    = cbTo;
    dseg.spacing  = spacingVal;
    dseg.Noisemode = 0;
    dseg.seed     = baseBrush->seed;

    SegResult r;
    int n = DrawLinear(&dseg, se->dabIndex, se->lastDabRad, outDabs, maxDabs, &r);

    if (n > 0 && toolMode == eSmudge) {
        outDabs[0].srcX = se->smudgeSrcPos.x;
        outDabs[0].srcY = se->smudgeSrcPos.y;
        for (int i = 1; i < n; i++) {
            outDabs[i].srcX = outDabs[i-1].x;
            outDabs[i].srcY = outDabs[i-1].y;
        }
        se->smudgeSrcPos = Vector2{outDabs[n-1].x, outDabs[n-1].y};
    }

    se->lastDabPos = r.lastDabPos;
    se->lastDabRad = r.lastRadOut;
    se->segBrushFrom.Realb = target;
    se->dabIndex += n;
    return n;
}

int StrokeEngine_FeedPoint(StrokeEngine* se, const StrokePoint& sp,
                           const d_RealBrush* baseBrush,
                           float initialAngle, int toolMode,
                           DrawDab* outDabs, int maxDabs) {
    if (!se->inStroke || maxDabs <= 0) return 0;

    Vector2 pos = {sp.x, sp.y};

    // Min distance gate (Spline mode)
    if (g_strokeSmoothingMode == SMOOTH_MODE_SPLINE && g_splineMinDist > 0.001f && se->splineCount >= 1) {
        float dist = Dist2D(se->splinePts[se->splineCount - 1], pos);
        if (dist < g_splineMinDist) return 0;
    }

    // Add to spline buffer (Spline mode)
    if (g_strokeSmoothingMode == SMOOTH_MODE_SPLINE) {
        if (se->splineCount < STROKE_SPLINE_POINTS) {
            se->splinePts[se->splineCount++] = pos;
        } else {
            memmove(se->splinePts, se->splinePts + 1, sizeof(Vector2) * (STROKE_SPLINE_POINTS - 1));
            se->splinePts[STROKE_SPLINE_POINTS - 1] = pos;
            if (se->processedCount > 0) se->processedCount--;
        }
    }

    // ── Mode: Linear ──
    if (g_strokeSmoothingMode == SMOOTH_MODE_LINEAR) {
        return FeedOnePoint(se, pos, sp.velocity, baseBrush, initialAngle, toolMode, outDabs, maxDabs);
    }

    // ── Mode: Smooth ──
    if (g_strokeSmoothingMode == SMOOTH_MODE_SMOOTH) {
        // Simple running average: blend current input with previous smoothed position
        if (se->smoothBufCount < 4) {
            se->smoothBuf[se->smoothBufCount++] = pos;
        } else {
            memmove(se->smoothBuf, se->smoothBuf + 1, sizeof(Vector2) * 3);
            se->smoothBuf[3] = pos;
        }
        // Average the buffer
        Vector2 avg = {0, 0};
        for (int i = 0; i < se->smoothBufCount; i++) {
            avg.x += se->smoothBuf[i].x;
            avg.y += se->smoothBuf[i].y;
        }
        avg.x /= se->smoothBufCount;
        avg.y /= se->smoothBufCount;
        return FeedOnePoint(se, avg, sp.velocity, baseBrush, initialAngle, toolMode, outDabs, maxDabs);
    }

    // ── Mode: Spline ──
    int totalDabs = 0;
    float vel = sp.velocity;
    int N = se->splineCount;

    // Stable segments have full 4-point support: i+2 < N → i <= N-3
    for (int seg = se->processedCount; seg <= N - 3 && totalDabs < maxDabs; seg++) {
        Vector2 p0, p1, p2, p3;
        if (seg == 0) {
            p0 = se->splinePts[0]; p1 = se->splinePts[0];
            p2 = se->splinePts[1]; p3 = se->splinePts[2];
        } else {
            p0 = se->splinePts[seg - 1];
            p1 = se->splinePts[seg];
            p2 = se->splinePts[seg + 1];
            p3 = se->splinePts[seg + 2];
        }
        for (int s = 0; s < STROKE_SPLINE_SUBDIVS; s++) {
            float tMid = ((float)s + 0.5f) / (float)STROKE_SPLINE_SUBDIVS;
            Vector2 mid = CatmullRom(p0, p1, p2, p3, tMid);
            int rem = maxDabs - totalDabs;
            if (rem <= 0) break;
            totalDabs += FeedOnePoint(se, mid, vel, baseBrush, initialAngle, toolMode,
                                      outDabs + totalDabs, rem);
        }
        se->processedCount = seg + 1;
    }

    return totalDabs;
}

void StrokeEngine_EndStroke(StrokeEngine* se) {
    se->inStroke = false;
    se->splineCount = 0;
    se->processedCount = 0;
    se->smoothBufCount = 0;
}

int StrokeEngine_FlushSmoothing(StrokeEngine* se, const d_RealBrush* baseBrush,
                                 float initialAngle, int toolMode,
                                 DrawDab* outDabs, int maxDabs) {
    if (!se->inStroke || maxDabs <= 0) return 0;
    if (g_strokeSmoothingMode != SMOOTH_MODE_SPLINE) return 0;

    float vel = 0.5f;
    int N = se->splineCount;
    int totalDabs = 0;

    // Flush deferred segment N-2 (with duplicated end) and any other unprocessed segments
    for (int seg = se->processedCount; seg <= N - 2 && totalDabs < maxDabs; seg++) {
        Vector2 p0, p1, p2, p3;
        if (seg == 0) {
            p0 = se->splinePts[0]; p1 = se->splinePts[0];
            p2 = se->splinePts[1]; p3 = (N > 2) ? se->splinePts[2] : se->splinePts[1];
        } else if (seg >= N - 2) {
            p0 = se->splinePts[seg - 1];
            p1 = se->splinePts[seg];
            p2 = se->splinePts[seg + 1];
            p3 = se->splinePts[seg + 1];
        } else {
            p0 = se->splinePts[seg - 1];
            p1 = se->splinePts[seg];
            p2 = se->splinePts[seg + 1];
            p3 = se->splinePts[seg + 2];
        }
        for (int s = 0; s < STROKE_SPLINE_SUBDIVS; s++) {
            float tMid = ((float)s + 0.5f) / (float)STROKE_SPLINE_SUBDIVS;
            Vector2 mid = CatmullRom(p0, p1, p2, p3, tMid);
            int rem = maxDabs - totalDabs;
            if (rem <= 0) break;
            totalDabs += FeedOnePoint(se, mid, vel, baseBrush, initialAngle, toolMode,
                                      outDabs + totalDabs, rem);
        }
    }

    return totalDabs;
}

// ── Utilities ─────────────────────────────────────────────────────────

void StrokeEngine_ApplyDabs(RenderTexture2D dstRT, Texture2D brushTex,
                            DrawDab* dabs, int n) {
    for (int i = 0; i < n; i++) {
        d_Brush tb; memset(&tb, 0, sizeof(tb));
        tb.Realb.rad_out     = dabs[i].brush.rad_out_px;
        tb.Realb.radInRatio  = dabs[i].brush.radInRatio;
        tb.Realb.opacity     = dabs[i].brush.opacity;
        tb.Realb.crv         = dabs[i].brush.crv;
        tb.Realb.x2y         = dabs[i].brush.scale_y;
        tb.Realb.resangle    = dabs[i].brush.resangle;
        tb.Realb.col         = dabs[i].brush.col;
        tb.Realb.cop         = dabs[i].brush.cop;
        tb.Realb.bmidx       = (uint8_t)dabs[i].brush.bmidx;
        tb.Realb.preserveop  = dabs[i].brush.preserveop;
        tb.Realb.eraseMode   = dabs[i].brush.eraseMode;
        tb.Realb.perspective = dabs[i].brush.perspective;
        tb.Realb.texScale    = dabs[i].brush.texScale;
        tb.Realb.texFeather  = dabs[i].brush.texFeather;
        tb.Realb.texThresh   = dabs[i].brush.texThresh;
        tb.Realb.texBlendVal = dabs[i].brush.texBlendVal;
        tb.Realb.texBlendMode = dabs[i].brush.texBlendMode;
        tb.Realb.texNoisemode = dabs[i].brush.texNoisemode;
        tb.Realb.texColorMode = dabs[i].brush.texColorMode;
        tb.Realb.useTexLumAsAlpha = dabs[i].brush.useTexLumAsAlpha;
        tb.Realb.pwr         = dabs[i].brush.pwr;
        BrushBlend_ApplyStamp(dstRT, &tb, brushTex,
                              dabs[i].x, dabs[i].y, dabs[i].srcX, dabs[i].srcY);
    }
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, float cx, float cy) {
    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    float radOut = baseBrush->rad_out;
    float spacingVal = BParam_GetValue(&bpSpacing);
    float segLen = radOut * 3.0f;          // length = 3 brush radii
    if (segLen < 2.0f) segLen = 2.0f;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    Vector2 start = {cx, cy};
    Vector2 end   = {cx + segLen * dirX, cy + segLen * dirY};

    CollapsedBrush cbFull = CollapseBrushParams(*baseBrush, 0.0f, eBrush);
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = 0;
    cbFull.baseSeed = 0;

    CollapsedBrush cbTiny = cbFull;
    cbTiny.rad_out_px = 1.0f;

    DrawDab dabs[512];
    SegResult r;

    // Single segment: radius shrinks from full to 1px over 3×rad_out length
    DrawSegment s;
    memset(&s, 0, sizeof(s));
    s.pos1 = start; s.pos2 = end;
    s.brushFrom = cbFull; s.brush = cbTiny;
    s.spacing = spacingVal;
    s.seed = baseBrush->seed;

    int n = DrawLinear(&s, 0, 0.0f, dabs, 256, &r);

    StrokeEngine_ApplyDabs(dstRT, brushTex, dabs, n);
}
