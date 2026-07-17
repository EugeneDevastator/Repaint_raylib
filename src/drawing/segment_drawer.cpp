#define _USE_MATH_DEFINES
#include "brush_draw.h"
#include "brush_blend.h"
#include "repaint.h"
#include "brush_preset.h"
#include <math.h>
#include <string.h>


// ── HSL conversion (matches the UI-side functions) ──────────────────
static float HueToRGB_Local(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static void RGBToHSL_Local(Color c, float& h, float& s, float& l) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    float d = mx - mn;
    l = (mx + mn) * 0.5f;
    if (d < 0.0001f) { h = 0; s = 0; return; }
    s = (l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r)       h = fmodf((g - b) / d + (g < b ? 6.0f : 0.0f), 6.0f) / 6.0f;
    else if (mx == g)  h = ((b - r) / d + 2.0f) / 6.0f;
    else               h = ((r - g) / d + 4.0f) / 6.0f;
}

static Color HSLToRGB_Local(float h, float s, float l) {
    if (s < 0.0001f) {
        uint8_t v = (uint8_t)(l * 255);
        return Color{v, v, v, 255};
    }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    return Color{
        (uint8_t)(HueToRGB_Local(p, q, h + 1.0f/3.0f) * 255),
        (uint8_t)(HueToRGB_Local(p, q, h) * 255),
        (uint8_t)(HueToRGB_Local(p, q, h - 1.0f/3.0f) * 255),
        255
    };
}

// ── Deterministic pseudo-random ────────────────────────────────────
static float RawRnd(uint16_t seed, float range) {
    uint32_t a = 64136401;
    uint32_t m = 25500;
    float x = (float)((a * (uint32_t)seed * (uint32_t)seed * (uint32_t)seed) % m);
    return x / 25500.0f * range;
}

// ── Blend ──────────────────────────────────────────────────────────
DabBrush BlendBrushes(DabBrush from, DabBrush to, float k) {
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    DabBrush r;
    r.rad_out_px = lerp(from.rad_out_px, to.rad_out_px, k);
    r.radInRatio = lerp(from.radInRatio, to.radInRatio, k);
    r.scale_x    = lerp(from.scale_x, to.scale_x, k);
    r.scale_y    = lerp(from.scale_y, to.scale_y, k);
    {   // angle-aware lerp — shortest path around the circle
        float a = from.resangle, b = to.resangle;
        float diff = fmodf(b - a, 360.0f);
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;
        r.resangle = fmodf(a + diff * k + 360.0f, 360.0f);
    }
    r.pwr        = lerp(from.pwr, to.pwr, k);
    r.opacity    = lerp(from.opacity, to.opacity, k);
    r.cop        = lerp(from.cop, to.cop, k);
    r.crv        = lerp(from.crv, to.crv, k);
    r.col.r = (uint8_t)(from.col.r + (to.col.r - from.col.r) * k);
    r.col.g = (uint8_t)(from.col.g + (to.col.g - from.col.g) * k);
    r.col.b = (uint8_t)(from.col.b + (to.col.b - from.col.b) * k);
    r.col.a = (uint8_t)(from.col.a + (to.col.a - from.col.a) * k);

    // Non-interpolated: carry from the start
    r.bmidx      = from.bmidx;
    r.preserveop = from.preserveop;
    r.eraseMode  = from.eraseMode;
    r.perspective = from.perspective;
    r.texScale   = from.texScale;
    r.texFeather = from.texFeather;
    r.texThresh  = from.texThresh;
    r.texBlendVal = from.texBlendVal;
    r.texBlendMode = from.texBlendMode;
    r.texNoisemode = from.texNoisemode;
    r.texColorMode = from.texColorMode;
    r.texTiling    = from.texTiling;
    r.useTexLumAsAlpha = from.useTexLumAsAlpha;
    r.userTexOriginX = from.userTexOriginX;
    r.userTexOriginY = from.userTexOriginY;
    r.userTexDirection = from.userTexDirection;
    r.focalOffset = from.focalOffset;

    // Jitter ranges (interpolated — rad jitRadOut overwritten per-dab in DrawLinear)
    r.jitRadOut = lerp(from.jitRadOut, to.jitRadOut, k);
    r.jitRadIn  = lerp(from.jitRadIn, to.jitRadIn, k);
    r.jitOpacity = lerp(from.jitOpacity, to.jitOpacity, k);
    r.jitCrv    = lerp(from.jitCrv, to.jitCrv, k);
    r.jitX2y    = lerp(from.jitX2y, to.jitX2y, k);
    r.jitHue    = lerp(from.jitHue, to.jitHue, k);
    r.jitSat    = lerp(from.jitSat, to.jitSat, k);
    r.jitLit    = lerp(from.jitLit, to.jitLit, k);
    r.jitCloneOp = from.jitCloneOp;
    r.jitFocal  = lerp(from.jitFocal, to.jitFocal, k);
    r.baseSeed  = from.baseSeed;
    r.scatter   = from.scatter;
    return r;
}

// ── Per-dab jitter ─────────────────────────────────────────────────
void JitterBrush(DabBrush& b, uint16_t baseSeed, int dabIdx) {
    float dr = RawRnd(baseSeed + (uint16_t)(dabIdx * 7 + 1), 1024) / 1024.0f * 2.0f - 1.0f;

    float baseRad = b.rad_out_px;  // save before jitter
    b.rad_out_px += dr * b.jitRadOut;
    // clamp: never go below half base or above double base, and hard min 0.5
    float radMin = fmaxf(0.5f, baseRad - b.jitRadOut);
    float radMax = fmaxf(radMin + 0.001f, baseRad + b.jitRadOut);
    b.rad_out_px = fmaxf(radMin, fminf(radMax, b.rad_out_px));

    b.radInRatio += dr * b.jitRadIn;
    if (b.radInRatio < 0.0f) b.radInRatio = 0.0f;
    if (b.radInRatio > 1.0f) b.radInRatio = 1.0f;

    b.opacity += dr * b.jitOpacity;
    if (b.opacity < 0.0f) b.opacity = 0.0f;
    if (b.opacity > 1.0f) b.opacity = 1.0f;

    b.crv += dr * b.jitCrv;
    if (b.crv < 0.0f) b.crv = 0.0f;
    if (b.crv > 1.0f) b.crv = 1.0f;

    b.scale_y += dr * b.jitX2y;
    if (b.scale_y < 0.01f) b.scale_y = 0.01f;
    if (b.scale_y > 1.0f) b.scale_y = 1.0f;

    b.cop += dr * b.jitCloneOp;
    if (b.cop < 0.0f) b.cop = 0.0f;
    if (b.cop > 1.0f) b.cop = 1.0f;

    b.focalOffset += dr * b.jitFocal;
    if (b.focalOffset < -1.0f) b.focalOffset = -1.0f;
    if (b.focalOffset > 1.0f)  b.focalOffset = 1.0f;

    // Color jitter: HSL
    float hue, sat, lit;
    RGBToHSL(b.col, hue, sat, lit);
    hue += dr * b.jitHue; if (hue < 0) hue += 1; else if (hue > 1) hue -= 1;
    sat += dr * b.jitSat; if (sat < 0) sat = 0; if (sat > 1) sat = 1;
    lit += dr * b.jitLit; if (lit < 0) lit = 0; if (lit > 1) lit = 1;
    b.col = HSLToRGB(hue, sat, lit);
}

// ── Helpers for iterative dab placement ───────────────────────────

// Given the last dab's radius and position, estimate the NEXT dab's
// un-jittered radius by assuming both radii are equal for the step.
static float FindNextDabRadius(float lastRad, float lastPos,
                               float segStart, float segEnd,
                               float segStartRad, float segEndRad,
                               float spacingMult)
{
    float segLen = segEnd - segStart;
    if (segLen < 0.0001f)
        return segStartRad;

    float slope = (segEndRad - segStartRad) / segLen;
    float rAtLast = segStartRad + slope * (lastPos - segStart);

    float denom = 1.0f - spacingMult * slope;
    float step;
    if (fabsf(denom) < 0.0001f) {
        step = (lastRad + rAtLast) * spacingMult;
    } else {
        step = spacingMult * (lastRad + rAtLast) / denom;
    }
    float minStep = spacingMult * 2.0f * fminf(lastRad, rAtLast);
    if (step < minStep) step = minStep;
    if (step < 1.0f) step = 1.0f;

    float nextPos = lastPos + step;
    float t = (nextPos - segStart) / segLen;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return segStartRad + (segEndRad - segStartRad) * t;
}


// Given the last dab's radius and position and the NEW dab's radius,
// find where the new dab should be placed so edges touch.
static float FindNextDabPosition(float lastRad, float lastPos,
                                  float nextRad, float spacingMult) {
    float step = (lastRad + nextRad) * spacingMult;
    if (step < 1.0f) step = 1.0f;
    return lastPos + step;
}

// ── Cubic Bezier ───────────────────────────────────────────────────
static Vector2 CubicBezier(Vector2 p0, Vector2 c0, Vector2 c1, Vector2 p1, float t) {
    float u = 1.0f - t;
    float u2 = u * u, u3 = u2 * u;
    float t2 = t * t, t3 = t2 * t;
    return Vector2{
        u3 * p0.x + 3.0f * u2 * t * c0.x + 3.0f * u * t2 * c1.x + t3 * p1.x,
        u3 * p0.y + 3.0f * u2 * t * c0.y + 3.0f * u * t2 * c1.y + t3 * p1.y
    };
}

// Walk along a pre-computed polyline (curvePts[0..n-1]) by `step` pixels
// starting from arc position `curPos`. Returns the new position and advances arcPos.
static Vector2 WalkArc(const Vector2* pts, int n, float& arcPos, float step, float totalLen) {
    float target = arcPos + step;
    if (target >= totalLen) { arcPos = totalLen; return pts[n-1]; }

    float walked = 0.0f;
    for (int i = 1; i < n; i++) {
        float dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y;
        float segLen = sqrtf(dx*dx + dy*dy);
        float segEnd = walked + segLen;
        if (segEnd >= target) {
            float t = (segLen > 0.0001f) ? (target - walked) / segLen : 0.0f;
            arcPos = target;
            return Vector2{pts[i-1].x + dx*t, pts[i-1].y + dy*t};
        }
        walked = segEnd;
    }
    arcPos = totalLen;
    return pts[n-1];
}

// ── Deterministic jittered radius (avoids full blend+jitter) ───────
static float JitterRadius(float unjitteredRad, float jitRange,
                          uint16_t baseSeed, int dabIdx) {
    float dr = RawRnd(baseSeed + (uint16_t)(dabIdx * 7 + 1), 1024) / 1024.0f * 2.0f - 1.0f;
    float r = unjitteredRad + dr * jitRange;
    float radMin = fmaxf(0.5f, unjitteredRad - jitRange);
    float radMax = fmaxf(radMin + 0.001f, unjitteredRad + jitRange);
    return fmaxf(radMin, fminf(radMax, r));
}

// ── EstimateSegmentStart (pure function) ──────────────────────────
Vector2 EstimateSegmentStart(SegmentData seg, float initialRad) {
    Vector2 from = seg.pos1;
    Vector2 to = seg.pos2;
    bool isCurved = (seg.ctrl0.x != from.x || seg.ctrl0.y != from.y ||
                     seg.ctrl3.x != to.x   || seg.ctrl3.y != to.y);
    float spacingMult = fmaxf(0.0f, seg.brushFrom.spacing);
    float rFrom = seg.brushFrom.rad_out_px;
    if (seg.pixelPerfect && seg.ppBias >= 0.0f) {
        int ip = (int)fmaxf(0.5f, rFrom);
        if (seg.ppBias == 0.0f && ip < 1) ip = 1;
        rFrom = (float)ip + seg.ppBias;
    }
    float lastRad = (initialRad > 0.0f) ? initialRad : rFrom;
    float startArc = (lastRad + rFrom) * spacingMult;

    Vector2 pts[65];
    float totalLen;
    if (isCurved) {
        totalLen = 0;
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64.0f;
            pts[i] = CubicBezier(from, seg.ctrl0, seg.ctrl3, to, t);
            if (i > 0)
                totalLen += sqrtf((pts[i].x - pts[i-1].x) * (pts[i].x - pts[i-1].x) +
                                   (pts[i].y - pts[i-1].y) * (pts[i].y - pts[i-1].y));
        }
    } else {
        float dx = to.x - from.x, dy = to.y - from.y;
        totalLen = sqrtf(dx*dx + dy*dy);
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64.0f;
            pts[i] = Vector2{from.x + dx * t, from.y + dy * t};
        }
    }
    if (startArc >= totalLen) return to;
    float tmp = 0.0f;
    return WalkArc(pts, 65, tmp, startArc, totalLen);
}

// ── DrawLinear ─────────────────────────────────────────────────────
int DrawLinear(const SegmentData& seg, int dabOffset, float initialRad,
               DabPoint* outPoints, int maxOut, SegResult* res) {
    if (!res) return 0;
    Vector2 from = seg.pos1;
    res->lastDabPos = from;
    res->lastSmudgeSrc = Vector2{seg.smudgeSrcX, seg.smudgeSrcY};
    res->lastRadOut = seg.brushFrom.rad_out_px;
    res->overdraw = 0.0f;

    Vector2 to = seg.pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));

    if (seg.tool == eSingleStamp) {
        if (outPoints) {
            outPoints[0].x = from.x; outPoints[0].y = from.y;
            outPoints[0].srcX = seg.smudgeSrcX; outPoints[0].srcY = seg.smudgeSrcY;
            outPoints[0].srcRad = seg.brushFrom.rad_out_px;
            outPoints[0].srcAngle = seg.brushFrom.resangle;
            outPoints[0].brush = seg.brushFrom;
        }
        res->lastDabPos = Vector2{from.x, from.y};
        res->lastSmudgeSrc = Vector2{from.x, from.y};
        res->lastRadOut = seg.brushFrom.rad_out_px;
        return 1;
    }

    if (stdist < 0.001f) return 0;

    float spacingMult = fmaxf(0.0f, seg.brushFrom.spacing);
    float rFrom = seg.brushFrom.rad_out_px;
    float rTo   = seg.brush.rad_out_px;
    bool isCurved = (seg.ctrl0.x != from.x || seg.ctrl0.y != from.y ||
                     seg.ctrl3.x != to.x   || seg.ctrl3.y != to.y);
    Vector2 curvePts[65];
    float totalLen = stdist;
    if (isCurved) {
        totalLen = 0;
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64.0f;
            curvePts[i] = CubicBezier(from, seg.ctrl0, seg.ctrl3, to, t);
            if (i > 0)
                totalLen += sqrtf((curvePts[i].x - curvePts[i-1].x) * (curvePts[i].x - curvePts[i-1].x) +
                                   (curvePts[i].y - curvePts[i-1].y) * (curvePts[i].y - curvePts[i-1].y));
        }
    } else {
        float dx = to.x - from.x, dy = to.y - from.y;
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64.0f;
            curvePts[i] = Vector2{from.x + dx * t, from.y + dy * t};
        }
    }
    float x2r = (to.x - from.x) / stdist;
    float y2r = (to.y - from.y) / stdist;
    if (seg.pixelPerfect && seg.ppBias >= 0.0f) {
        auto snapRad = [&](float r) {
            int ip = (int)fmaxf(0.5f, r);
            if (seg.ppBias == 0.0f && ip < 1) ip = 1;
            return (float)ip + seg.ppBias;
        };
        rFrom = snapRad(rFrom);
        rTo   = snapRad(rTo);
    }
Vector2 startpos = EstimateSegmentStart(seg, initialRad);
    return BuildSegment(seg, dabOffset, initialRad, outPoints, maxOut, res,
                        startpos,
                        spacingMult, rFrom, rTo, isCurved, curvePts,
                        totalLen, x2r, y2r);
}

// ── BuildSegment ──────────────────────────────────────────────────
int BuildSegment(const SegmentData& seg, int dabOffset, float initialRad,
                  DabPoint* outPoints, int maxOut, SegResult* res,
                  Vector2 startPos, float spacingMult, float rFrom, float rTo,
                  bool isCurved, const Vector2* curvePts,
                  float totalLen, float x2r, float y2r) {
    Vector2 from = seg.pos1;
    Vector2 to = seg.pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    // Convert startPos to arc distance
    float startArc = 0.0f;
    if (isCurved) {
        float walked = 0.0f;
        for (int i = 1; i <= 64; i++) {
            float dx = curvePts[i].x - curvePts[i-1].x;
            float dy = curvePts[i].y - curvePts[i-1].y;
            float segLen = sqrtf(dx*dx + dy*dy);
            if (segLen > 0.0001f) {
                float t = ((startPos.x - curvePts[i-1].x) * dx + (startPos.y - curvePts[i-1].y) * dy) / (segLen * segLen);
                if (t >= 0.0f && t <= 1.0f) { startArc = walked + t * segLen; break; }
            }
            walked += segLen;
            if (i == 64) startArc = totalLen;
        }
    } else if (stdist > 0.001f) {
        startArc = ((startPos.x - from.x) * (to.x - from.x) + (startPos.y - from.y) * (to.y - from.y)) / stdist;
    }
    float lastDabPos = 0.0f;
    float lastDabRad = (initialRad > 0.0f) ? initialRad : rFrom;
    res->firstDabPos = Vector2{0, 0};
    res->lastRadOut = lastDabRad;
    int count = 0;
    float lastSrcX = seg.smudgeSrcX;
    float lastSrcY = seg.smudgeSrcY;
    float lastSrcRad = seg.brushFrom.rad_out_px;
    float lastSrcAngle = seg.brushFrom.resangle;

    while (count < maxOut) {
        // 1. Estimate next position using last (jittered) radius
        float tNext_est = lastDabPos / totalLen;
        float nextRadEst = rFrom + (rTo - rFrom) * tNext_est;
        float nextArc_est = lastDabPos + (lastDabRad + nextRadEst) * spacingMult;
        if (nextArc_est > totalLen) break;

        // 2. Sample unjittered radius at estimated position
        float tNext = nextArc_est / totalLen;
        if (tNext > 1.0f) tNext = 1.0f;
        float nextRadUnJit = rFrom + (rTo - rFrom) * tNext;

        // 3. Apply jitter to get ACTUAL next radius
        float jitRange = seg.brushFrom.jitRadOut;
        float nextRadJit = JitterRadius(nextRadUnJit, jitRange, seg.brushFrom.baseSeed, dabOffset + count);

        // 4. Find exact position using jittered next radius
        float nextArc = lastDabPos + (lastDabRad + nextRadJit) * spacingMult;
        if (nextArc <= lastDabPos + 0.5f) nextArc = lastDabPos + 0.5f; // prevent lock
        if (nextArc > totalLen) break;

        // 5. Get curve position
        float arcPos = lastDabPos;
        Vector2 pos = WalkArc(curvePts, 65, arcPos, nextArc - lastDabPos, totalLen);

        // 6. Build final brush
        float k = nextArc / totalLen;
        if (k > 1.0f) k = 1.0f;
        DabBrush dabCB = BlendBrushes(seg.brushFrom, seg.brush, k);
        dabCB.rad_out_px = nextRadUnJit;
        // Per-dab jitter range — proportional to this dab's own radius
        float jitFrac = seg.brushFrom.jitRadOut / fmaxf(0.001f, seg.brushFrom.rad_out_px);
        dabCB.jitRadOut = fmaxf(0.0f, dabCB.rad_out_px * jitFrac);

        // Per-dab angle: drive brush rotation from curve tangent.
        // Note: this does NOT update csDir — the stroke direction modulator
        // is set from the segment chord by the emitter (via Modulator module),
        // which is far more stable than a per-dab tangent sample.
        float tx = 0, ty = 0;
        {
            float t = nextArc / totalLen;
            if (t < 0) t = 0; if (t > 1) t = 1;
            int idx = (int)(t * 64);
            if (idx < 0) idx = 0; if (idx > 63) idx = 63;
            tx = curvePts[idx+1].x - curvePts[idx].x;
            ty = curvePts[idx+1].y - curvePts[idx].y;
        }

        // Scatter: shift perpendicular to travel (before jitter — use unjittered radius)
        float scatterRad = dabCB.scatter * dabCB.rad_out_px;
        Vector2 scatterPos = pos;
        if (scatterRad > 0.001f) {
            float len = sqrtf(tx*tx + ty*ty);
            if (len > 0.001f) {
                uint16_t segMix = (uint16_t)((int)seg.pos1.x * 73) ^ (uint16_t)((int)seg.pos1.y * 137);
                uint16_t seed = seg.brushFrom.baseSeed + segMix + (uint16_t)(count * 13 + 37);
                float raw = RawRnd(seed, 1024);
                float u = raw / 1024.0f;
                float off = (u * 2.0f - 1.0f) * scatterRad;
                scatterPos.x += (-ty / len) * off;
                scatterPos.y += (tx / len) * off;
            }
        }
        pos = scatterPos;

        JitterBrush(dabCB, seg.brushFrom.baseSeed, dabOffset + count);

        // Pixel-perfect: lock radius parity (bias set per-stroke in emitter)
        if (seg.pixelPerfect && seg.ppBias >= 0.0f) {
            float r = fmaxf(0.5f, dabCB.rad_out_px);
            int ip = (int)r;
            if (seg.ppBias == 0.0f && ip < 1) ip = 1;
            dabCB.rad_out_px = (float)ip + seg.ppBias;
        }

        if (count == 0) res->firstDabPos = pos;
        res->lastDabPos = pos;

        if (outPoints) {
            outPoints[count].x = pos.x;
            outPoints[count].y = pos.y;
            outPoints[count].srcX = lastSrcX;
            outPoints[count].srcY = lastSrcY;
            outPoints[count].srcRad = lastSrcRad;
            outPoints[count].srcAngle = lastSrcAngle;
            outPoints[count].brush = dabCB;
        }
        lastSrcX = pos.x;
        lastSrcY = pos.y;
        lastSrcRad = dabCB.rad_out_px;
        lastSrcAngle = dabCB.resangle;

        lastDabPos = nextArc;
        lastDabRad = dabCB.rad_out_px;  // JITTERED — actual placed size
        count++;
    }

    if (count > 0) {
        res->lastRadOut = lastDabRad;
    }
    res->lastSmudgeSrc = Vector2{lastSrcX, lastSrcY};
    return count;
}


// ── DrawSegment ────────────────────────────────────────────────────
int DrawSegment(const SegmentData& dseg, RenderTexture2D rt, Texture2D brushTex, bool useTexture, bool seamless, int dabOffset, bool pixelPerfect) {
    static DabPoint pts[65536];
    SegResult r;
    int cnt = DrawLinear(dseg, dabOffset, 0.0f, pts, 65536, &r);

    for (int i = 0; i < cnt; i++)
        BrushBlend_ApplyStamp(rt, pts[i].brush, brushTex, useTexture,
                              pts[i].x, pts[i].y, pts[i].srcX, pts[i].srcY,
                              pts[i].srcRad, pts[i].srcAngle,
                              seamless, pixelPerfect);
    return cnt;
}

// ── SegDrawer helpers (computation only, no rendering) ─────────────

void SegDrawer_SetSegmentStart(float startRad, Vector2 startPos, SegmentData* seg) {
    seg->pos1 = startPos;
    if (startRad > 0.0f)
        seg->brushFrom.rad_out_px = startRad;
}

int SegDrawer_ComputeSegmentEnd(const SegmentData& seg, int dabOffset, float initialRad,
                                 Vector2* outLastPos, float* outLastRad) {
    SegResult r;
    int cnt = DrawLinear(seg, dabOffset, initialRad, nullptr, 65536, &r);
    *outLastPos = r.lastDabPos;
    *outLastRad = r.lastRadOut;
    return cnt;
}
