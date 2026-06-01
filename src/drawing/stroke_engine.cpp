#include "repaint.h"
#include "replay_recorder.h"
#include "stroke_engine.h"
#include "stroke.h"
#include <math.h>

int g_strokeSmoothingMode = SMOOTH_MODE_SMOOTH;
float g_strokeThrottle = 0.0f;
ICommandBroker* g_broker = nullptr;

static inline float BaseModVal(const BParam& bp, float cpar) {
    float rng = bp.run.clipmaxF - bp.run.clipminF;
    float base = cpar * rng + bp.run.clipminF;
    base = fminf(fmaxf(base, 0.0f), 1.0f);
    return base * (bp.outMax - bp.outMin) + bp.outMin;
}

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
    cb.userTexOriginX = b.userTexOriginX;
    cb.userTexOriginY = b.userTexOriginY;
    cb.userTexDirection = b.userTexDirection;
    cb.spacing = BParam_GetValue(&bpSpacing);
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

void StrokeEngine_Init(StrokeEngine* se) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->lastDabPos = Vector2{0, 0};
    se->inStroke = false;
    se->prevSegPos = Vector2{0, 0};
    se->prevSegDir = Vector2{0, 0};
    se->prevSegLen = 0.0f;
    se->prevVel = 0.0f;
    se->initDir = 0.0f;
    se->initDirSet = false;
    se->dabIndex = 0;
    se->splineCount = 0;
    se->processedCount = 0;
    se->accumDist = 0.0f;
    se->lastInputPos = Vector2{0, 0};
    se->targetType = 0;
    se->targetId = 0;
    memset(se->splinePts, 0, sizeof(se->splinePts));
}

void StrokeEngine_BeginStroke(StrokeEngine* se, const d_Brush* baseBrush, float x, float y) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->segBrushFrom = *baseBrush;
    se->lastDabPos = Vector2{x, y};
    se->inStroke = true;
    se->prevSegPos = Vector2{x, y};
    se->prevSegDir = Vector2{0, 0};
    se->prevSegLen = 0.0f;
    se->prevVel = 0.0f;
    se->initDirSet = false;
    se->dabIndex = 0;
    se->splineCount = 1;
    se->processedCount = 0;
    se->accumDist = 0.0f;
    se->lastInputPos = Vector2{x, y};
    memset(se->splinePts, 0, sizeof(se->splinePts));
    se->splinePts[0] = Vector2{x, y};
}

static void BuildModulatedBrush(StrokeEngine* se,
                                 Vector2 from, Vector2 to,
                                 float velocity,
                                 const d_RealBrush* baseBrush,
                                 float initialAngle, int toolMode,
                                 d_RealBrush& target, float& spacingVal) {
    float segDx = to.x - from.x;
    float segDy = to.y - from.y;
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

    se->prevSegPos = to;
    se->prevSegDir = Vector2{segDx, segDy};
    se->prevSegLen = segLen;
    se->prevVel = velocity;

    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);

    target = *baseBrush;
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
    spacingVal = BParam_GetValue(&bpSpacing);
}

static int FeedOnePoint(StrokeEngine* se, Vector2 pos, float velocity,
                        const d_RealBrush* baseBrush,
                        float initialAngle, int toolMode,
                        Vector2 overrideCtrl0, Vector2 overrideCtrl3) {
    if (!se->inStroke) return 0;

    d_RealBrush target;
    float spacingVal;
    BuildModulatedBrush(se, se->lastDabPos, pos, velocity,
                        baseBrush, initialAngle, toolMode,
                        target, spacingVal);

    CollapsedBrush cbFrom = CollapseBrushParams(se->segBrushFrom.Realb, initialAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initialAngle, toolMode);

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1     = se->lastDabPos;
    dseg.pos2     = pos;
    dseg.ctrl0    = overrideCtrl0;
    dseg.ctrl3    = overrideCtrl3;
    dseg.brushFrom = cbFrom;
    dseg.brush    = cbTo;
    dseg.Noisemode = 0;
    dseg.tool      = (uint8_t)toolMode;
    dseg.seamless  = g_seamlessPaint ? 1 : 0;
    dseg.seed      = baseBrush->seed;
    dseg.smudgeSrcX = se->lastDabPos.x;
    dseg.smudgeSrcY = se->lastDabPos.y;
    dseg.targetType = se->targetType;
    dseg.targetId   = se->targetId;

    if (g_recorder) g_recorder->on_segment(dseg);
    if (g_broker) g_broker->on_segment(dseg);

    SegResult r;
    DrawLinear(&dseg, se->dabIndex, 0.0f, nullptr, nullptr, 65536, &r);

    se->lastDabPos = r.lastDabPos;
    se->segBrushFrom.Realb = target;
    se->dabIndex += (int)(r.lastDabPos.x != se->lastDabPos.x || r.lastDabPos.y != se->lastDabPos.y);
    return 1;
}

int StrokeEngine_FeedPoint(StrokeEngine* se, const StrokePoint& sp,
                           const d_RealBrush* baseBrush,
                           float initialAngle, int toolMode) {
    if (!se->inStroke) return 0;

    Vector2 pos = {sp.x, sp.y};

    if (g_strokeSmoothingMode == SMOOTH_MODE_LINEAR) {
        return FeedOnePoint(se, pos, sp.velocity, baseBrush, initialAngle, toolMode,
                            se->lastDabPos, pos);
    }

    float threshold = fmaxf(g_strokeThrottle, 0.5f);
    if (se->splineCount < 4)
        threshold = fminf(threshold, 5.0f);

    float dist = Dist2D(se->lastInputPos, pos);
    se->accumDist += dist;
    se->lastInputPos = pos;

    if (se->accumDist >= threshold) {
        if (se->splineCount < STROKE_SPLINE_POINTS) {
            se->splinePts[se->splineCount++] = pos;
        } else {
            memmove(se->splinePts, se->splinePts + 1, sizeof(Vector2) * (STROKE_SPLINE_POINTS - 1));
            se->splinePts[STROKE_SPLINE_POINTS - 1] = pos;
            if (se->processedCount > 0) se->processedCount--;
        }
        se->accumDist = 0;
    }

    int totalDabs = 0;
    float vel = sp.velocity;
    int N = se->splineCount;

    for (int seg = se->processedCount; seg <= N - 3; seg++) {
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

        float segLen = Dist2D(p1, p2);
        if (segLen < 0.5f) {
            se->processedCount = seg + 1;
            continue;
        }

        d_RealBrush target;
        float spacingVal;
        BuildModulatedBrush(se, p1, p2, vel,
                            baseBrush, initialAngle, toolMode,
                            target, spacingVal);

        CollapsedBrush cbFrom = CollapseBrushParams(se->segBrushFrom.Realb, initialAngle, toolMode);
        CollapsedBrush cbTo   = CollapseBrushParams(target, initialAngle, toolMode);

        DrawSegment dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1      = se->lastDabPos;
        dseg.pos2      = p2;
        dseg.ctrl0     = p0;
        dseg.ctrl3     = p3;
        dseg.brushFrom = cbFrom;
        dseg.brush     = cbTo;
        dseg.Noisemode = 0;
        dseg.tool      = (uint8_t)toolMode;
        dseg.seamless  = g_seamlessPaint ? 1 : 0;
        dseg.seed      = baseBrush->seed;
        dseg.smudgeSrcX = se->lastDabPos.x;
        dseg.smudgeSrcY = se->lastDabPos.y;
        dseg.targetType = se->targetType;
        dseg.targetId   = se->targetId;

        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);

        SegResult r;
        DrawLinear(&dseg, se->dabIndex, 0.0f, nullptr, nullptr, 65536, &r);

        if (r.lastDabPos.x != se->lastDabPos.x || r.lastDabPos.y != se->lastDabPos.y) {
            se->lastDabPos = r.lastDabPos;
            totalDabs++;
        }
        se->segBrushFrom.Realb = target;
        se->processedCount = seg + 1;
    }

    return totalDabs > 0 ? totalDabs : 0;
}

int StrokeEngine_FlushSmoothing(StrokeEngine* se, const d_RealBrush* baseBrush,
                                  float initialAngle, int toolMode) {
    if (!se->inStroke) return 0;
    if (g_strokeSmoothingMode != SMOOTH_MODE_SMOOTH) return 0;

    float vel = 0.5f;
    int N = se->splineCount;
    int totalDabs = 0;

    for (int seg = se->processedCount; seg <= N - 2; seg++) {
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

        float segLen = Dist2D(p1, p2);
        if (segLen < 0.5f) continue;

        d_RealBrush target;
        float spacingVal;
        BuildModulatedBrush(se, p1, p2, vel,
                            baseBrush, initialAngle, toolMode,
                            target, spacingVal);

        CollapsedBrush cbFrom = CollapseBrushParams(se->segBrushFrom.Realb, initialAngle, toolMode);
        CollapsedBrush cbTo   = CollapseBrushParams(target, initialAngle, toolMode);

        DrawSegment dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1      = se->lastDabPos;
        dseg.pos2      = p2;
        dseg.ctrl0     = p0;
        dseg.ctrl3     = p3;
        dseg.brushFrom = cbFrom;
        dseg.brush     = cbTo;
        dseg.Noisemode = 0;
        dseg.tool      = (uint8_t)toolMode;
        dseg.seamless  = g_seamlessPaint ? 1 : 0;
        dseg.seed      = baseBrush->seed;
        dseg.smudgeSrcX = se->lastDabPos.x;
        dseg.smudgeSrcY = se->lastDabPos.y;
        dseg.targetType = se->targetType;
        dseg.targetId   = se->targetId;

        if (g_recorder) g_recorder->on_segment(dseg);
        if (g_broker) g_broker->on_segment(dseg);

        SegResult r;
        DrawLinear(&dseg, se->dabIndex, 0.0f, nullptr, nullptr, 65536, &r);

        if (r.lastDabPos.x != se->lastDabPos.x || r.lastDabPos.y != se->lastDabPos.y) {
            se->lastDabPos = r.lastDabPos;
            totalDabs++;
        }
        se->segBrushFrom.Realb = target;
    }

    return totalDabs;
}

void StrokeEngine_EndStroke(StrokeEngine* se) {
    se->inStroke = false;
    se->splineCount = 0;
    se->processedCount = 0;
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, int toolMode,
                              float cx, float cy) {
    float spacingVal = BParam_GetValue(&bpSpacing);
    float radOut = baseBrush->rad_out;
    float segLen = radOut * 3.0f;
    if (segLen < 2.0f) segLen = 2.0f;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    Vector2 start = {cx, cy};
    Vector2 end   = {cx + segLen * dirX, cy + segLen * dirY};

    CollapsedBrush cbFull = CollapseBrushParams(*baseBrush, 0.0f, toolMode);
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = 0;
    cbFull.baseSeed = 0;
    cbFull.spacing = spacingVal;

    CollapsedBrush cbTiny = cbFull;
    cbTiny.rad_out_px = 1.0f;

    DrawSegment seed;
    memset(&seed, 0, sizeof(seed));
    seed.pos1 = seed.pos2 = Vector2{cx, cy};
    seed.ctrl0 = seed.ctrl3 = seed.pos1;
    seed.brushFrom = seed.brush = cbFull;
    seed.seed = baseBrush->seed;
    seed.tool = eSingleStamp;
    seed.seamless = g_seamlessPaint ? 1 : 0;
    seed.smudgeSrcX = cx;
    seed.smudgeSrcY = cy;
    DrawOneSegment(seed, dstRT, brushTex, seed.seamless != 0);

    DrawSegment s;
    memset(&s, 0, sizeof(s));
    s.pos1 = start; s.pos2 = end;
    s.brushFrom = cbFull; s.brush = cbTiny;
    s.seed = baseBrush->seed;
    s.tool = (uint8_t)toolMode;
    s.seamless = g_seamlessPaint ? 1 : 0;
    s.smudgeSrcX = cx;
    s.smudgeSrcY = cy;
    DrawOneSegment(s, dstRT, brushTex, s.seamless != 0);
}
