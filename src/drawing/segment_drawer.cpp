#include "brush_draw.h"
#include "repaint.h"
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

// ── DrawLinear ─────────────────────────────────────────────────────
int DrawLinear(const DrawSegment* seg, int dabOffset, float initialRad,
               void (*apply)(float x, float y, float srcX, float srcY, const CollapsedBrush& brush, void* user),
               void* user, int maxOut, SegResult* res) {
    if (maxOut <= 0 || !res) return 0;
    Vector2 from = seg->pos1;
    res->lastDabPos = from;
    res->lastRadOut = seg->brushFrom.rad_out_px;
    res->overdraw = 0.0f;

    Vector2 to = seg->pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    if (stdist < 0.001f) return 0;

    if (seg->tool == eSingleStamp) {
        if (apply) apply(from.x, from.y, seg->smudgeSrcX, seg->smudgeSrcY, seg->brushFrom, user);
        res->lastDabPos = Vector2{from.x, from.y};
        res->lastRadOut = seg->brushFrom.rad_out_px;
        return 1;
    }

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
    float lastSrcX = seg->smudgeSrcX;
    float lastSrcY = seg->smudgeSrcY;

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
        CollapsedBrush tempCB = BlendBrushes(seg->brushFrom, seg->brush, tNext);
        tempCB.rad_out_px = nextRadUnJit;
        JitterBrush(tempCB, seg->brushFrom.baseSeed, dabOffset + count);
        float nextRadJit = tempCB.rad_out_px;

        // 4. Find exact position using jittered next radius
        float nextArc = lastDabPos + (lastDabRad + nextRadJit) * spacingMult;
        if (nextArc > totalLen) break;

        // 5. Get curve position
        float arcPos = lastDabPos;
        Vector2 pos = WalkArc(curvePts, 65, arcPos, nextArc - lastDabPos, totalLen);

        // 6. Build final brush (jitter call 2 — deterministic, same result)
        float k = nextArc / totalLen;
        if (k > 1.0f) k = 1.0f;
        CollapsedBrush dabCB = BlendBrushes(seg->brushFrom, seg->brush, k);
        dabCB.rad_out_px = nextRadUnJit;
        JitterBrush(dabCB, seg->brushFrom.baseSeed, dabOffset + count);

        if (apply) apply(pos.x, pos.y, lastSrcX, lastSrcY, dabCB, user);
        lastSrcX = pos.x;
        lastSrcY = pos.y;

        lastDabPos = nextArc;
        lastDabRad = dabCB.rad_out_px;  // JITTERED — actual placed size
        count++;
    }

    if (count > 0) {
        res->lastRadOut = lastDabRad;
        if (isCurved) {
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


// ── ApplyCollapsedBrush ─────────────────────────────────────────────
void ApplyCollapsedBrush(RenderTexture2D rt, const CollapsedBrush& cb,
                         float x, float y, float srcX, float srcY, Texture2D brushTex) {
    d_Brush tb; memset(&tb, 0, sizeof(tb));
    tb.Realb.rad_out = cb.rad_out_px;
    tb.Realb.radInRatio = cb.radInRatio;
    tb.Realb.opacity = cb.opacity;
    tb.Realb.crv = cb.crv;
    tb.Realb.x2y = cb.scale_y;
    tb.Realb.resangle = cb.resangle;
    tb.Realb.col = cb.col;
    tb.Realb.cop = cb.cop;
    tb.Realb.bmidx = (uint8_t)cb.bmidx;
    tb.Realb.preserveop = cb.preserveop;
    tb.Realb.eraseMode = cb.eraseMode;
    tb.Realb.perspective = cb.perspective;
    tb.Realb.texScale = cb.texScale;
    tb.Realb.texFeather = cb.texFeather;
    tb.Realb.texThresh = cb.texThresh;
    tb.Realb.texBlendVal = cb.texBlendVal;
    tb.Realb.texBlendMode = cb.texBlendMode;
    tb.Realb.texNoisemode = cb.texNoisemode;
    tb.Realb.texColorMode = cb.texColorMode;
    tb.Realb.useTexLumAsAlpha = cb.useTexLumAsAlpha;
    tb.Realb.pwr = cb.pwr;
    tb.Realb.userTexOriginX = cb.userTexOriginX;
    tb.Realb.userTexOriginY = cb.userTexOriginY;
    tb.Realb.userTexDirection = cb.userTexDirection;
    BrushBlend_ApplyStamp(rt, &tb, brushTex, x, y, srcX, srcY);
}

// ── DrawOneSegment ─────────────────────────────────────────────────
void DrawOneSegment(const DrawSegment& dseg, RenderTexture2D rt, Texture2D brushTex, bool seamless, int dabOffset) {
    bool savedSeamless = g_seamlessPaint;
    g_seamlessPaint = seamless;

    struct UserData { RenderTexture2D* rt; Texture2D tex; };
    UserData ud = {&rt, brushTex};

    auto cb = [](float x, float y, float srcX, float srcY, const CollapsedBrush& brush, void* user) {
        UserData* ud = (UserData*)user;
        ApplyCollapsedBrush(*ud->rt, brush, x, y, srcX, srcY, ud->tex);
    };

    SegResult r;
    DrawLinear(&dseg, dabOffset, 0.0f, cb, &ud, 65536, &r);

    g_seamlessPaint = savedSeamless;
}

// ── SegDrawer helpers (computation only, no rendering) ─────────────

void SegDrawer_SetSegmentStart(float startRad, Vector2 startPos, DrawSegment* seg) {
    seg->pos1 = startPos;
    if (startRad > 0.0f)
        seg->brushFrom.rad_out_px = startRad;
}

void SegDrawer_ComputeSegmentEnd(const DrawSegment* seg, int dabOffset, float initialRad,
                                  Vector2* outLastPos, float* outLastRad) {
    SegResult r;
    DrawLinear(seg, dabOffset, initialRad, nullptr, nullptr, 65536, &r);
    *outLastPos = r.lastDabPos;
    *outLastRad = r.lastRadOut;
}
