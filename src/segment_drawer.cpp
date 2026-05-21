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

    // Jitter ranges (carry from start, used by JitterBrush)
    r.jitRadOut = from.jitRadOut;
    r.jitRadIn  = from.jitRadIn;
    r.jitOpacity = from.jitOpacity;
    r.jitCrv    = from.jitCrv;
    r.jitX2y    = from.jitX2y;
    r.jitHue    = from.jitHue;
    r.jitSat    = from.jitSat;
    r.jitLit    = from.jitLit;
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

// ── DrawLinear ─────────────────────────────────────────────────────
int DrawLinear(const DrawSegment* seg, DrawDab* out, int maxOut, SegResult* res) {
    if (maxOut <= 0 || !res) return 0;
    Vector2 from = seg->pos1;
    res->lastDabPos = from;
    res->lastRadOut = seg->brushFrom.rad_out_px;
    res->overdraw = 0.0f;

    Vector2 to = seg->pos2;
    float stdist = sqrtf((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y));
    if (stdist < 0.001f) return 0;

    float spacingMult = seg->spacing;
    if (spacingMult < 0.0f) spacingMult = 0.0f;

    float dx = to.x - from.x, dy = to.y - from.y;
    float x2r = dx / stdist, y2r = dy / stdist;

    float curPos = 0.0f;
    int count = 0;
    float noisex = 0, noisey = 0;
    uint16_t nn = 0;

    while (curPos < stdist && count < maxOut) {
        float k = curPos / stdist;
        CollapsedBrush cb = BlendBrushes(seg->brushFrom, seg->brush, k);

        // Jitter this dab (including rad_out_px for spacing)
        JitterBrush(cb, seg->brushFrom.baseSeed, count);

        float step = cb.rad_out_px * 2.0f * spacingMult;
        if (step < 1.0f) step = 1.0f;

        float nextPos = curPos + step;
        if (nextPos > stdist) break;
        curPos = nextPos;

        nn++;
        Vector2 pos = {from.x + curPos * x2r, from.y + curPos * y2r};

        // Scatter (deterministic noise perpendicular to stroke)
        if (seg->Noisemode != 255) {
            // Per-dab noise for texture variations (stored in output brush)
            float ns = 0, ny = 0;
            ApplyNoise(cb, seg->Noisemode, seg->seed, pos, nn, ns, ny);
            (void)ns; (void)ny;
        }

        // Re-apply jitter for the output (same seed, so same result)
        // JitterBrush was already called above — the output cb is already jittered
        out[count].x = pos.x;
        out[count].y = pos.y;
        out[count].srcX = pos.x;
        out[count].srcY = pos.y;
        out[count].brush = cb;
        count++;
    }

    if (count > 0) {
        res->lastRadOut = out[count-1].brush.rad_out_px;
        res->lastDabPos = Vector2{from.x + curPos * x2r, from.y + curPos * y2r};
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

    float spacingMult = seg->spacing;
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
