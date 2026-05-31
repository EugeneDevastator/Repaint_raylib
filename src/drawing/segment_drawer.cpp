#include "brush_draw.h"
#include <math.h>
#include <string.h>

// ── HSL conversion (matches the UI-side functions) ──────────────────
static float HueToRGB(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static void RGBToHSL(Color c, float& h, float& s, float& l) {
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

static Color HSLToRGB(float h, float s, float l) {
    if (s < 0.0001f) {
        uint8_t v = (uint8_t)(l * 255);
        return Color{v, v, v, 255};
    }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    return Color{
        (uint8_t)(HueToRGB(p, q, h + 1.0f/3.0f) * 255),
        (uint8_t)(HueToRGB(p, q, h) * 255),
        (uint8_t)(HueToRGB(p, q, h - 1.0f/3.0f) * 255),
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

// ── ApplyNoise ─────────────────────────────────────────────────────
static void ApplyNoise(CollapsedBrush& b, int noisemode, uint16_t seed,
                       Vector2 pos, uint16_t n, float& noisex, float& noisey) {
    (void)b;
    if (noisemode == 0) {
        noisex = (float)(RawRnd(seed + n * 3, 1024) * 1024.0f);
        noisey = (float)(RawRnd(seed + n + 21, 1024) * 1024.0f);
    } else if (noisemode == 1) {
        noisex = 34.0f;
        noisey = 76.0f;
    } else {
        noisex = fmaxf(0, (int)pos.x);
        noisey = fmaxf(0, (int)pos.y);
    }
    noisex = noisex - 1024.0f * (float)(int)(noisex / 1024.0f);
    noisey = noisey - 1024.0f * (float)(int)(noisey / 1024.0f);
}

// ── Blend ──────────────────────────────────────────────────────────
CollapsedBrush BlendBrushes(CollapsedBrush from, CollapsedBrush to, float k) {
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    CollapsedBrush r;
    r.rad_out_px = lerp(from.rad_out_px, to.rad_out_px, k);
    r.radInRatio = lerp(from.radInRatio, to.radInRatio, k);
    r.scale_x    = lerp(from.scale_x, to.scale_x, k);
    r.scale_y    = lerp(from.scale_y, to.scale_y, k);
    r.resangle   = lerp((float)from.resangle, (float)to.resangle, k);
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
    r.useTexLumAsAlpha = from.useTexLumAsAlpha;
    r.userTexOriginX = from.userTexOriginX;
    r.userTexOriginY = from.userTexOriginY;
    r.userTexDirection = from.userTexDirection;

    // Jitter ranges (interpolated — proportional to radius)
    r.jitRadOut = lerp(from.jitRadOut, to.jitRadOut, k);
    r.jitRadIn  = lerp(from.jitRadIn, to.jitRadIn, k);
    r.jitOpacity = lerp(from.jitOpacity, to.jitOpacity, k);
    r.jitCrv    = lerp(from.jitCrv, to.jitCrv, k);
    r.jitX2y    = lerp(from.jitX2y, to.jitX2y, k);
    r.jitHue    = lerp(from.jitHue, to.jitHue, k);
    r.jitSat    = lerp(from.jitSat, to.jitSat, k);
    r.jitLit    = lerp(from.jitLit, to.jitLit, k);
    r.jitCloneOp = from.jitCloneOp;
    r.baseSeed  = from.baseSeed;
    return r;
}

// ── Per-dab jitter ─────────────────────────────────────────────────
void JitterBrush(CollapsedBrush& b, uint16_t baseSeed, int dabIdx) {
    float dr = RawRnd(baseSeed + (uint16_t)(dabIdx * 7 + 1), 1024) / 1024.0f * 2.0f - 1.0f;

    b.rad_out_px += dr * b.jitRadOut;
    if (b.rad_out_px < 0.5f) b.rad_out_px = 0.5f;

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
        // degenerate: radius grows as fast as spacing, fallback
        step = (lastRad + rAtLast) * spacingMult;
    } else {
        step = spacingMult * (lastRad + rAtLast) / denom;
    }
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

// ── Catmull-Rom ────────────────────────────────────────────────────
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

// Walk along a pre-computed polyline (curvePts[0..n-1]) by `step` pixels
// starting from arc position `curPos`. Returns the new position and advances arcPos.
static Vector2 WalkArc(Vector2* pts, int n, float& arcPos, float step, float totalLen) {
    float target = arcPos + step;
    if (target > totalLen) target = totalLen;
    if (target < 0) target = 0;

    float remaining = target;
    for (int i = 1; i < n; i++) {
        float segLen = sqrtf((pts[i].x - pts[i-1].x) * (pts[i].x - pts[i-1].x) +
                              (pts[i].y - pts[i-1].y) * (pts[i].y - pts[i-1].y));
        if (remaining <= segLen || i == n - 1) {
            float t = (segLen > 0.001f) ? remaining / segLen : 0;
            if (t < 0) t = 0; if (t > 1) t = 1;
            arcPos = target;
            return Vector2{pts[i-1].x + (pts[i].x - pts[i-1].x) * t,
                           pts[i-1].y + (pts[i].y - pts[i-1].y) * t};
        }
        remaining -= segLen;
    }
    arcPos = target;
    return pts[n - 1];
}

// ── DrawLinear ─────────────────────────────────────────────────────
int DrawLinear(const DrawSegment* seg, int dabOffset, float initialRad, DrawDab* out, int maxOut, SegResult* res) {
    if (maxOut <= 0 || !res) return 0;
    Vector2 from = seg->pos1;
    res->lastDabPos = from;
    res->lastRadOut = seg->brushFrom.rad_out_px;
    res->overdraw = 0.0f;

    Vector2 to = seg->pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    if (stdist < 0.001f) return 0;

    bool isCurved = (seg->ctrl0.x != from.x || seg->ctrl0.y != from.y ||
                     seg->ctrl3.x != to.x   || seg->ctrl3.y != to.y);

    float spacingMult = seg->brushFrom.spacing;
    if (spacingMult < 0.0f) spacingMult = 0.0f;

    float rFrom = seg->brushFrom.rad_out_px;
    float rTo   = seg->brush.rad_out_px;

    // Pre-compute curve polyline (65 points for 64 subdivisions)
    Vector2 curvePts[65];
    float totalLen = stdist;
    if (isCurved) {
        totalLen = 0;
        for (int i = 0; i <= 64; i++) {
            float t = (float)i / 64.0f;
            curvePts[i] = CatmullRom(seg->ctrl0, from, to, seg->ctrl3, t);
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

    float dx = to.x - from.x, dy = to.y - from.y;
    float x2r = dx / stdist, y2r = dy / stdist;

    float lastDabPos = 0.0f;
    float lastDabRad = (initialRad > 0.0f) ? initialRad : rFrom;
    res->lastRadOut = lastDabRad;
    int count = 0;
    uint16_t nn = 0;

    while (lastDabPos < totalLen - 1.0f && count < maxOut) {
        // 1. Find next dab's un-jittered base radius
        float k = lastDabPos / totalLen;
        float baseRad = rFrom + (rTo - rFrom) * k;
        float nextDabRad = FindNextDabRadius(lastDabRad, lastDabPos,
                                             0.0f, totalLen, rFrom, rTo, spacingMult);

        // 2. Randomize the next dab's radius
        float dr = RawRnd(seg->brushFrom.baseSeed + (uint16_t)((dabOffset + count) * 7 + 1), 1024) / 1024.0f * 2.0f - 1.0f;
        nextDabRad += dr * seg->brushFrom.jitRadOut;

        // 3. Find position along the curve where the randomized radius touches the last dab
        float nextArc = FindNextDabPosition(lastDabRad, lastDabPos, nextDabRad, spacingMult);
        if (nextArc > totalLen) break;

        // 4. Get position on curve at this arc distance
        float arcPos = lastDabPos; // dummy, will be updated by WalkArc
        Vector2 pos = WalkArc(curvePts, 65, arcPos, nextArc - lastDabPos, totalLen);
        lastDabPos = nextArc;

        // 5. Build brush at the actual position, jitter visual params
        float k2 = lastDabPos / totalLen;
        CollapsedBrush dabCB = BlendBrushes(seg->brushFrom, seg->brush, k2);
        JitterBrush(dabCB, seg->brushFrom.baseSeed, dabOffset + count);

        nn++;
        out[count].x = pos.x;
        out[count].y = pos.y;
        out[count].srcX = pos.x;
        out[count].srcY = pos.y;
        out[count].brush = dabCB;
        count++;

        lastDabRad = dabCB.rad_out_px;
    }

    if (count > 0) {
        res->lastRadOut = out[count-1].brush.rad_out_px;
        if (isCurved) {
            // Last arc position on the curve
            float t = (lastDabPos / totalLen);
            if (t < 0) t = 0; if (t > 1) t = 1;
            float idxF = t * 64;
            int idx = (int)idxF;
            float frac = idxF - idx;
            if (idx < 0) idx = 0;
            if (idx > 63) idx = 63;
            Vector2 lp;
            lp.x = curvePts[idx].x + (curvePts[idx+1].x - curvePts[idx].x) * frac;
            lp.y = curvePts[idx].y + (curvePts[idx+1].y - curvePts[idx].y) * frac;
            res->lastDabPos = lp;
        } else {
            res->lastDabPos = Vector2{from.x + lastDabPos * x2r, from.y + lastDabPos * y2r};
        }
    }
    return count;
}


// ── DrawAirflow ────────────────────────────────────────────────────
int DrawAirflow(const DrawSegment* seg, float accum, DrawDab* out, int maxOut, SegResult* res) {
    if (maxOut <= 0 || !res) return 0;
    Vector2 from = seg->pos1;
    res->lastDabPos = from;
    res->lastRadOut = seg->brushFrom.rad_out_px;
    res->overdraw = 0.0f;

    Vector2 to = seg->pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    if (stdist < 0.001f) return 0;

    float spacingMult = seg->brushFrom.spacing;
    if (spacingMult < 0.0f) spacingMult = 0.0f;

    float dx = to.x - from.x, dy = to.y - from.y;
    float x2r = dx / stdist, y2r = dy / stdist;

    float tdist = stdist + accum;
    float firstSpacing = seg->brushFrom.rad_out_px * 2.0f * spacingMult;
    if (firstSpacing < 1.0f) firstSpacing = 1.0f;

    if (tdist < firstSpacing) {
        res->overdraw = tdist;
        return 0;
    }

    float firstDist = firstSpacing - accum;
    if (firstDist < 0.0f) firstDist = 0.0f;
    float remaining = stdist - firstDist;
    int extra = (remaining > 0.0f) ? (int)(remaining / firstSpacing) : 0;
    int count = 0;
    float dabbable = fmaxf(stdist - firstDist, 0.001f);
    uint16_t nn = 0;

    for (int i = 0; i <= extra && count < maxOut; i++) {
        float d = firstDist + i * firstSpacing;
        if (d > stdist) break;

        nn++;
        float k = fminf((d - firstDist) / dabbable, 1.0f);
        CollapsedBrush cb = BlendBrushes(seg->brushFrom, seg->brush, k);
        JitterBrush(cb, seg->brushFrom.baseSeed, count);

        Vector2 pos = {from.x + d * x2r, from.y + d * y2r};
        out[count].x = pos.x;
        out[count].y = pos.y;
        out[count].srcX = pos.x;
        out[count].srcY = pos.y;
        out[count].brush = cb;
        count++;
    }

    if (count > 0) {
        float lastD = firstDist + (count - 1) * firstSpacing;
        res->lastRadOut = out[count-1].brush.rad_out_px;
        res->lastDabPos = Vector2{from.x + lastD * x2r, from.y + lastD * y2r};
        res->overdraw = stdist - lastD;
    } else {
        res->overdraw = accum + stdist;
    }
    return count;
}
