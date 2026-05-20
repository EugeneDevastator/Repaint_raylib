#include "repaint.h"
#include "stroke_engine.h"
#include "stroke.h"
#include <math.h>

bool g_strokeSmoothing = true;

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

static void ApplyNoise(d_Brush* cbrush, int noisemode, uint16_t seed, Vector2 pos, uint16_t n) {
    if (noisemode == 0) {
        cbrush->Realb.noisex = (uint16_t)(Stroke_RawRnd(seed + n * 3, 1024) * 1024.0f);
        cbrush->Realb.noisey = (uint16_t)(Stroke_RawRnd(seed + n + 21, 1024) * 1024.0f);
    } else if (noisemode == 1) {
        cbrush->Realb.noisex = 34;
        cbrush->Realb.noisey = 76;
    } else if (noisemode == 2) {
        cbrush->Realb.noisex = (uint16_t)fmaxf(0, (int)pos.x);
        cbrush->Realb.noisey = (uint16_t)fmaxf(0, (int)pos.y);
    }
    cbrush->Realb.noisex = (uint16_t)(cbrush->Realb.noisex
        - 1024 * (int)(cbrush->Realb.noisex / 1024));
    cbrush->Realb.noisey = (uint16_t)(cbrush->Realb.noisey
        - 1024 * (int)(cbrush->Realb.noisey / 1024));
}

static void PerDabJitter(BrushDab* dabs, int n, float sizeMulFactor, uint16_t baseSeed) {
    for (int i = 0; i < n; i++) {
        float dr = Stroke_RawRnd(baseSeed + (uint16_t)(i * 7 + 1), 1024) / 1024.0f * 2.0f - 1.0f;
        d_RealBrush& b = dabs[i].brush;

        { float d = dr * 2.0f * bpSize.user.jitter * (bpSize.outMax - bpSize.outMin) * sizeMulFactor;
          float rawMin = bpSize.outMin * sizeMulFactor, rawMax = bpSize.outMax * sizeMulFactor;
          b.rad_out += d; b.rad_out = fmaxf(rawMin, fminf(rawMax, b.rad_out)); }

        { float h = b.rad_in / fmaxf(b.rad_out, 0.001f);
          float d = dr * 2.0f * bpHardness.user.jitter * (bpHardness.outMax - bpHardness.outMin);
          h += d; h = fmaxf(0.0f, fminf(1.0f, h)); b.rad_in = b.rad_out * h; }

        { float d = dr * 2.0f * bpCurvature.user.jitter * (bpCurvature.outMax - bpCurvature.outMin);
          b.crv += d; b.crv = fmaxf(0.0f, fminf(1.0f, b.crv)); }

        { float d = dr * 2.0f * bpOpacity.user.jitter * (bpOpacity.outMax - bpOpacity.outMin);
          b.opacity += d; b.opacity = fmaxf(0.0f, fminf(1.0f, b.opacity)); }

        { float d = dr * 2.0f * bpScaleRel.user.jitter * (bpScaleRel.outMax - bpScaleRel.outMin);
          b.x2y += d; b.x2y = fmaxf(0.0f, fminf(1.0f, b.x2y)); }

        { float h, s, l; RGBToHSL(b.col, h, s, l);
          h += dr * 2.0f * bpQuickHue.user.jitter * (bpQuickHue.outMax - bpQuickHue.outMin);
          s += dr * 2.0f * bpQuickSat.user.jitter * (bpQuickSat.outMax - bpQuickSat.outMin);
          l += dr * 2.0f * bpQuickLit.user.jitter * (bpQuickLit.outMax - bpQuickLit.outMin);
          h = fmodf(h, 1.0f); if (h < 0) h += 1.0f;
          s = fmaxf(0.0f, fminf(1.0f, s)); l = fmaxf(0.0f, fminf(1.0f, l));
          b.col = HSLToRGB(h, s, l); }

        { float d = dr * 2.0f * bpCloneOpacity.user.jitter * (bpCloneOpacity.outMax - bpCloneOpacity.outMin);
          b.cop += d; b.cop = fmaxf(0.0f, fminf(1.0f, b.cop)); }

        { float baseFactor = sizeMulFactor;
          float raw = BParam_GetValue(&bpSizeMul) + dr * 2.0f * bpSizeMul.user.jitter * (bpSizeMul.outMax - bpSizeMul.outMin);
          raw = fmaxf(bpSizeMul.outMin, fminf(bpSizeMul.outMax, raw));
          float jitteredFactor = powf(16.0f, raw / 128.0f - 1.0f);
          float ratio = (baseFactor > 0.0001f) ? jitteredFactor / baseFactor : 1.0f;
          b.rad_out *= ratio; b.rad_in *= ratio; }

        float scatterVal = GetModVal(&bpScatter);
        if (scatterVal > 0.001f) {
            float scatterOffset = scatterVal * b.rad_out * 0.5f;
            dabs[i].x += (Stroke_RawRnd(baseSeed + (uint16_t)(i * 11 + 2), 1024) / 1024.0f * 2.0f - 1.0f) * scatterOffset;
            dabs[i].y += (Stroke_RawRnd(baseSeed + (uint16_t)(i * 13 + 3), 1024) / 1024.0f * 2.0f - 1.0f) * scatterOffset;
        }
    }
}

static inline float BaseModVal(const BParam& bp, float cpar) {
    float rng = bp.run.clipmaxF - bp.run.clipminF;
    float base = cpar * rng + bp.run.clipminF;
    base = fminf(fmaxf(base, 0.0f), 1.0f);
    return base * (bp.outMax - bp.outMin) + bp.outMin;
}

// ── LinearStroke: even dab spacing from lastDabPos ─────────────────────

int SegmentDrawer_DrawLinear(const d_Section* section, BrushDab* outDabs,
                             int maxDabs, SegmentResult* outResult) {
    if (maxDabs <= 0 || !outResult) return 0;
    Vector2 from = section->Stroke.pos1;
    outResult->lastDabPos = from;
    outResult->overdraw = 0.0f;

    Vector2 to = section->Stroke.pos2;
    float stdist = Dist2D(from, to);
    if (stdist < 0.001f) return 0;

    float spacing = fmaxf(section->spacing, 1.0f);
    int maxDab = (int)(stdist / spacing);
    if (maxDab < 1) return 0;

    float dx = to.x - from.x, dy = to.y - from.y;
    float x2r = dx / stdist, y2r = dy / stdist;
    float rrang = section->Brush.Realb.rad_out * (section->scatter / 51.0f);
    uint16_t n = 0;

    for (int i = 1; i <= maxDab && i - 1 < maxDabs; i++) {
        float d = i * spacing;
        Vector2 pos = {from.x + d * x2r, from.y + d * y2r};

        n++;
        float rnflw = Stroke_RawRnd(section->BrushFrom.Realb.seed + n * 2, 1024) * rrang * 2.0f - rrang;
        pos.x -= rnflw * y2r;
        pos.y += rnflw * x2r;

        float k = (maxDab > 1) ? (float)(i - 1) / (float)(maxDab - 1) : 0.5f;
        d_Brush cbrush = section->BrushFrom;
        cbrush.Realb = Stroke_BlendBrushes(section->BrushFrom.Realb, section->Brush.Realb, k);
        ApplyNoise(&cbrush, section->Noisemode, section->BrushFrom.Realb.seed, pos, n);

        int idx = i - 1;
        outDabs[idx].x = pos.x;
        outDabs[idx].y = pos.y;
        outDabs[idx].srcX = pos.x;
        outDabs[idx].srcY = pos.y;
        outDabs[idx].brush = cbrush.Realb;
    }

    int count = (maxDab < maxDabs) ? maxDab : maxDabs;
    if (count > 0) {
        float lastD = count * spacing;
        outResult->lastDabPos = Vector2{from.x + lastD * x2r, from.y + lastD * y2r};
    }
    return count;
}

// ── AirflowStroke: bursty dab placement with overdraw accumulation ─────

int SegmentDrawer_DrawAirflow(const d_Section* section, float initialDabAccum,
                              BrushDab* outDabs, int maxDabs,
                              SegmentResult* outResult) {
    if (maxDabs <= 0 || !outResult) return 0;
    outResult->lastDabPos = section->Stroke.pos1;
    outResult->overdraw = 0.0f;

    Vector2 from = section->Stroke.pos1, to = section->Stroke.pos2;
    float stdist = Dist2D(from, to);
    if (stdist < 0.001f) return 0;

    float dx = to.x - from.x, dy = to.y - from.y;
    float x2r = dx / stdist, y2r = dy / stdist;

    float tdist = stdist + initialDabAccum;
    float spacing = fmaxf(section->spacing, 1.0f);
    if (tdist < spacing) {
        outResult->overdraw = tdist;
        outResult->lastDabPos = from;
        return 0;
    }

    float firstDist = spacing - initialDabAccum;
    if (firstDist < 0.0f) firstDist = 0.0f;
    float remaining = stdist - firstDist;
    int extraDabs = (remaining > 0.0f) ? (int)(remaining / spacing) : 0;
    int count = 0;

    float rrang = section->Brush.Realb.rad_out * (section->scatter / 51.0f);
    uint16_t n = 0;
    float dabbable = fmaxf(stdist - firstDist, 0.001f);

    for (int i = 0; i <= extraDabs && count < maxDabs; i++) {
        float d = firstDist + i * spacing;
        if (d > stdist) break;

        Vector2 pos = {from.x + d * x2r, from.y + d * y2r};
        n++;
        float rnflw = Stroke_RawRnd(section->BrushFrom.Realb.seed + n * 2, 1024) * rrang * 2.0f - rrang;
        pos.x -= rnflw * y2r;
        pos.y += rnflw * x2r;

        float k = fminf((d - firstDist) / dabbable, 1.0f);
        d_Brush cbrush = section->BrushFrom;
        cbrush.Realb = Stroke_BlendBrushes(section->BrushFrom.Realb, section->Brush.Realb, k);
        ApplyNoise(&cbrush, section->Noisemode, section->BrushFrom.Realb.seed, pos, n);

        outDabs[count].x = pos.x;
        outDabs[count].y = pos.y;
        outDabs[count].srcX = pos.x;
        outDabs[count].srcY = pos.y;
        outDabs[count].brush = cbrush.Realb;
        count++;
    }

    if (count > 0) {
        float lastDabDist = firstDist + (count - 1) * spacing;
        outResult->lastDabPos = Vector2{from.x + lastDabDist * x2r, from.y + lastDabDist * y2r};
        outResult->overdraw = stdist - lastDabDist;
    } else {
        outResult->overdraw = initialDabAccum + stdist;
    }
    return count;
}

// ── StrokeEngine implementation ─────────────────────────────────────────

void StrokeEngine_Init(StrokeEngine* se) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->lastDabPos = Vector2{0, 0};
    se->dabAccum = 0.0f;
    se->smudgeSrcPos = Vector2{0, 0};
    se->inStroke = false;
    se->prevSegPos = Vector2{0, 0};
    se->prevSegDir = Vector2{0, 0};
    se->prevSegLen = 0.0f;
    se->prevVel = 0.0f;
    se->initDir = 0.0f;
    se->initDirSet = false;
    se->splineCount = 0;
    se->strokeSmoothing = true;
}

void StrokeEngine_BeginStroke(StrokeEngine* se, const d_Brush* baseBrush, float x, float y) {
    memset(&se->segBrushFrom, 0, sizeof(se->segBrushFrom));
    se->segBrushFrom = *baseBrush;
    se->lastDabPos = Vector2{x, y};
    se->dabAccum = 0.0f;
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
    se->strokeSmoothing = g_strokeSmoothing;
}

static int FeedOnePointLinear(StrokeEngine* se, Vector2 pos, float velocity,
                              const d_RealBrush* baseBrush,
                              float initialAngle, int toolMode,
                              BrushDab* outDabs, int maxDabs) {
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

    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    float spacingBaseRad = BParam_GetValue(&bpSize) * sizeMulFactor;
    float spacingVal = BParam_GetValue(&bpSpacing);
    float spacing = fmaxf(spacingBaseRad * 2.0f * spacingVal, 1.0f);

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

    d_Section section;
    memset(&section, 0, sizeof(section));
    section.Stroke.pos1 = se->lastDabPos;
    section.Stroke.pos2 = pos;
    section.BrushFrom = se->segBrushFrom;
    section.Brush.Realb = target;
    section.spacing = spacing;
    section.scatter = 0;
    section.Noisemode = 0;
    section.BrushFrom.Realb.seed = baseBrush->seed;

    SegmentResult result;
    int n = SegmentDrawer_DrawLinear(&section, outDabs, maxDabs, &result);

    if (n > 0) {
        if (toolMode == eSmudge) {
            outDabs[0].srcX = se->smudgeSrcPos.x;
            outDabs[0].srcY = se->smudgeSrcPos.y;
            for (int i = 1; i < n; i++) {
                outDabs[i].srcX = outDabs[i-1].x;
                outDabs[i].srcY = outDabs[i-1].y;
            }
            se->smudgeSrcPos = Vector2{outDabs[n-1].x, outDabs[n-1].y};
        }
        PerDabJitter(outDabs, n, sizeMulFactor, baseBrush->seed);
    }

    se->lastDabPos = result.lastDabPos;
    se->segBrushFrom.Realb = target;
    return n;
}

int StrokeEngine_FeedPoint(StrokeEngine* se, const StrokePoint& sp,
                           const d_RealBrush* baseBrush,
                           float initialAngle, int toolMode,
                           BrushDab* outDabs, int maxDabs) {
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
        return FeedOnePointLinear(se, pos, sp.velocity, baseBrush, initialAngle, toolMode, outDabs, maxDabs);
    }

    // Spline mode: walk midpoints, accumulate distance, feed segment when length >= spacing
    float splineSpacing = fmaxf(BParam_GetValue(&bpSize) * 
        powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f) * 2.0f * BParam_GetValue(&bpSpacing), 1.0f);
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
            totalDabs += FeedOnePointLinear(se, mid, vel, baseBrush, initialAngle, toolMode,
                                            outDabs + totalDabs, rem);
            accDist = 0.0f;
            accStart = se->lastDabPos;
        }
    }
    return totalDabs;
}

void StrokeEngine_EndStroke(StrokeEngine* se) {
    se->inStroke = false;
    se->dabAccum = 0.0f;
    se->splineCount = 0;
}

// ── Utilities ─────────────────────────────────────────────────────────

void StrokeEngine_ApplyDabs(RenderTexture2D dstRT, Texture2D brushTex, BrushDab* dabs, int n) {
    for (int i = 0; i < n; i++) {
        d_Brush tb; memset(&tb, 0, sizeof(tb));
        tb.Realb = dabs[i].brush;
        BrushBlend_ApplyStamp(dstRT, &tb, brushTex, dabs[i].x, dabs[i].y, dabs[i].srcX, dabs[i].srcY);
    }
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, float cx, float cy) {
    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    float spacingBaseRad = BParam_GetValue(&bpSize) * sizeMulFactor;
    float spacingVal = BParam_GetValue(&bpSpacing);
    float spacing = fmaxf(spacingBaseRad * 2.0f * spacingVal, 1.0f);

    float maxSegLen = 200.0f;
    float segLen = fminf(baseBrush->rad_out * 8.0f, maxSegLen);
    if (segLen < spacing) segLen = spacing;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    Vector2 start = {cx, cy};
    Vector2 mid = {cx + segLen * dirX, cy + segLen * dirY};

    float dir2X = -0.8f, dir2Y = -0.2f;
    float dir2Len = sqrtf(dir2X * dir2X + dir2Y * dir2Y);
    dir2X /= dir2Len; dir2Y /= dir2Len;
    Vector2 end = {mid.x + segLen * 0.7f * dir2X, mid.y + segLen * 0.7f * dir2Y};

    BrushDab dabs[512];

    d_Section s1;
    memset(&s1, 0, sizeof(s1));
    s1.Stroke.pos1 = start; s1.Stroke.pos2 = mid;
    s1.BrushFrom.Realb = *baseBrush; s1.Brush.Realb = *baseBrush;
    s1.spacing = spacing; s1.scatter = 0; s1.Noisemode = 0;
    s1.BrushFrom.Realb.seed = baseBrush->seed;

    int total = 0;
    SegmentResult r;
    int n1 = SegmentDrawer_DrawLinear(&s1, dabs + total, 256, &r);

    d_Section s2;
    memset(&s2, 0, sizeof(s2));
    s2.Stroke.pos1 = mid; s2.Stroke.pos2 = end;
    s2.BrushFrom.Realb = *baseBrush; s2.Brush.Realb = *baseBrush;
    s2.spacing = spacing; s2.scatter = 0; s2.Noisemode = 0;
    s2.BrushFrom.Realb.seed = baseBrush->seed;

    int n2 = SegmentDrawer_DrawLinear(&s2, dabs + total + n1, 256, &r);

    StrokeEngine_ApplyDabs(dstRT, brushTex, dabs, n1 + n2);
}
