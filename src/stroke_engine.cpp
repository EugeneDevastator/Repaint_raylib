#include "repaint.h"
#include "stroke_engine.h"
#include "stroke.h"
#include <math.h>

bool g_strokeSmoothing = false;

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
    cb.radInRatio = (b.rad_out > 0.001f) ? b.rad_in / b.rad_out : 0.0f;
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
    se->splinePts[0] = Vector2{0, 0};
    se->dabIndex = 0;
    se->lastDabRad = 0.0f;
    se->strokeSmoothing = false;
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
    se->splineCount = 0;
    se->splinePts[0] = Vector2{x, y};
    se->splineCount = 1;
    se->dabIndex = 0;
    se->lastDabRad = 0.0f;
    se->strokeSmoothing = g_strokeSmoothing;
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
    target.rad_in   = target.rad_out * hVal;
    target.crv      = BaseModVal(bpCurvature,  g_modPars.Pars[bpCurvature.penMode]);
    target.opacity  = BaseModVal(bpOpacity,    g_modPars.Pars[bpOpacity.penMode]);
    target.resangle = fmodf(initialAngle + BaseModVal(bpAngle, g_modPars.Pars[bpAngle.penMode]), 360.0f);
    target.x2y      = BaseModVal(bpScaleRel,   g_modPars.Pars[bpScaleRel.penMode]);
    target.col      = HSLToRGB(BaseModVal(bpQuickHue, g_modPars.Pars[bpQuickHue.penMode]),
                               BaseModVal(bpQuickSat, g_modPars.Pars[bpQuickSat.penMode]),
                               BaseModVal(bpQuickLit, g_modPars.Pars[bpQuickLit.penMode]));
    target.cop = (toolMode == eSmudge) ? BaseModVal(bpCloneOpacity, g_modPars.Pars[bpCloneOpacity.penMode]) : 0.0f;
    target.rad_out *= sizeMulFactor;
    target.rad_in  *= sizeMulFactor;

    float spacingVal = BParam_GetValue(&bpSpacing);

    // Collapse both endpoint brushes
    CollapsedBrush cbFrom = CollapseBrushParams(se->segBrushFrom.Realb, initialAngle, toolMode);
    CollapsedBrush cbTo   = CollapseBrushParams(target, initialAngle, toolMode);

    DrawSegment dseg;
    memset(&dseg, 0, sizeof(dseg));
    dseg.pos1     = se->lastDabPos;
    dseg.pos2     = pos;
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

    if (se->splineCount < STROKE_SPLINE_POINTS) {
        se->splinePts[se->splineCount++] = pos;
    } else {
        se->splinePts[0] = se->splinePts[1];
        se->splinePts[1] = se->splinePts[2];
        se->splinePts[2] = se->splinePts[3];
        se->splinePts[3] = pos;
    }

    if (!se->strokeSmoothing || se->splineCount < STROKE_SPLINE_POINTS) {
        return FeedOnePoint(se, pos, sp.velocity, baseBrush, initialAngle, toolMode, outDabs, maxDabs);
    }

    // Spline mode
    float spacingVal = BParam_GetValue(&bpSpacing);
    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    float rad = BParam_GetValue(&bpSize) * sizeMulFactor;
    float splineSpacing = fmaxf(rad * 2.0f * spacingVal, 1.0f);

    int totalDabs = 0;
    float vel = sp.velocity;
    Vector2 accStart = se->lastDabPos;
    float accDist = 0.0f;

    for (int s = 0; s < STROKE_SPLINE_SUBDIVS; s++) {
        float tMid = ((float)s + 0.5f) / (float)STROKE_SPLINE_SUBDIVS;
        Vector2 mid = CatmullRom(se->splinePts[1], se->splinePts[2],
                                  se->splinePts[3], se->splinePts[3], tMid);

        float dSeg = Dist2D(accStart, mid);
        accDist += dSeg;
        accStart = mid;

        if (accDist >= splineSpacing) {
            int rem = maxDabs - totalDabs;
            if (rem <= 0) break;
            totalDabs += FeedOnePoint(se, mid, vel, baseBrush, initialAngle, toolMode,
                                      outDabs + totalDabs, rem);
            accDist = 0.0f;
            accStart = se->lastDabPos;
        }
    }
    return totalDabs;
}

void StrokeEngine_EndStroke(StrokeEngine* se) {
    se->inStroke = false;
    se->splineCount = 0;
}

// ── Utilities ─────────────────────────────────────────────────────────

void StrokeEngine_ApplyDabs(RenderTexture2D dstRT, Texture2D brushTex,
                            DrawDab* dabs, int n) {
    for (int i = 0; i < n; i++) {
        d_Brush tb; memset(&tb, 0, sizeof(tb));
        tb.Realb.rad_out     = dabs[i].brush.rad_out_px;
        tb.Realb.rad_in      = dabs[i].brush.rad_out_px * dabs[i].brush.radInRatio;
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
    float spacingBaseRad = BParam_GetValue(&bpSize) * sizeMulFactor;
    float spacingVal = BParam_GetValue(&bpSpacing);
    float minPxSpacing = fmaxf(spacingBaseRad * 2.0f * spacingVal, 1.0f);

    float maxSegLen = 200.0f;
    float segLen = fminf(baseBrush->rad_out * 8.0f, maxSegLen);
    if (segLen < minPxSpacing) segLen = minPxSpacing;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    Vector2 start = {cx, cy};
    Vector2 mid = {cx + segLen * dirX, cy + segLen * dirY};

    float dir2X = -0.8f, dir2Y = -0.2f;
    float dir2Len = sqrtf(dir2X * dir2X + dir2Y * dir2Y);
    dir2X /= dir2Len; dir2Y /= dir2Len;
    Vector2 end = {mid.x + segLen * 0.7f * dir2X, mid.y + segLen * 0.7f * dir2Y};

    CollapsedBrush cb = CollapseBrushParams(*baseBrush, 0.0f, eBrush);
    CollapsedBrush cbSmall = cb;
    cbSmall.rad_out_px = 1.0f;
    cbSmall.jitRadOut = bpSize.user.jitter * 1.0f;

    DrawSegment s1, s2;
    memset(&s1, 0, sizeof(s1));
    s1.pos1 = start; s1.pos2 = mid;
    s1.brushFrom = cb; s1.brush = cb;
    s1.spacing = spacingVal;
    s1.seed = baseBrush->seed;

    memset(&s2, 0, sizeof(s2));
    s2.pos1 = mid; s2.pos2 = end;
    s2.brushFrom = cb; s2.brush = cbSmall;
    s2.spacing = spacingVal;
    s2.seed = baseBrush->seed;

    DrawDab dabs[512];
    SegResult r;
    int total = 0;
    int n1 = DrawLinear(&s1, 0, 0.0f, dabs + total, 256, &r);
    total += n1;

    s2.pos1 = r.lastDabPos;
    int n2 = DrawLinear(&s2, 0, r.lastRadOut, dabs + total, 256, &r);

    StrokeEngine_ApplyDabs(dstRT, brushTex, dabs, n1 + n2);
}
