#include "repaint.h"
#include "stroke.h"
#include <math.h>
#include <string.h>

BrushInterpolator::BrushInterpolator() {
    memset(&segBrushFrom, 0, sizeof(segBrushFrom));
    lastDabPos = Vector2{0, 0};
    prevInputPos = Vector2{0, 0};
    smudgeSrcPos = Vector2{0, 0};
    dabAccum = 0.0f;
    inStroke = false;
}

void BrushInterpolator::BeginStroke(const d_Brush& userBrush, float startX, float startY) {
    memset(&segBrushFrom, 0, sizeof(segBrushFrom));
    segBrushFrom = userBrush;
    lastDabPos = Vector2{startX, startY};
    prevInputPos = Vector2{startX, startY};
    smudgeSrcPos = Vector2{startX, startY};
    dabAccum = 0.0f;
    inStroke = true;
}

void BrushInterpolator::EndStroke() {
    inStroke = false;
    dabAccum = 0.0f;
}

int BrushInterpolator::FeedStrokePoint(
    const StrokePoint& pt, const d_RealBrush& targetBrush,
    InputEvent* out, int maxOut,
    float spacing, int toolMode)
{
    if (!inStroke || maxOut <= 0) return 0;

    // Qt: measure input-to-input distance, not last-dab-to-input
    Vector2 from = prevInputPos;
    Vector2 to = {pt.x, pt.y};
    float stdist = Dist2D(from, to);
    if (stdist < 0.001f) return 0;

    // Qt formula?: SpacingCtl^2 * rad_out * ScaleCtl * RadCtl, using current brush radius
    // Spacing is pre-computed from the base (unmodulated) brush radius
    // to avoid gaps when velocity-modulated rad_out is small
    spacing = fmaxf(spacing, 1.0f);

    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float x2r = dx / stdist;
    float y2r = dy / stdist;

    float tdist = stdist + dabAccum;
    if (tdist < spacing) {
        dabAccum += stdist;
        prevInputPos = to;
        return 0;
    }

    float firstDist = spacing - dabAccum;
    if (firstDist < 0.0f) firstDist = 0.0f;
    float remaining = stdist - firstDist;
    int extraDabs = (remaining > 0.0f) ? (int)(remaining / spacing) : 0;
    int count = 0;

    for (int i = 0; i <= extraDabs && count < maxOut; i++) {
        float d = firstDist + i * spacing;
        if (d > stdist) break;

        Vector2 pos = {from.x + d * x2r, from.y + d * y2r};

        // Brush interpolation: remap k so first dab starts at segBrushFrom (k=0)
        // and last dab reaches targetBrush (k=1), regardless of firstDist offset
        float dabbable = fmaxf(stdist - firstDist, 0.001f);
        float k = fminf((d - firstDist) / dabbable, 1.0f);
        d_RealBrush ib = Stroke_BlendBrushes(segBrushFrom.Realb, targetBrush, k);

        InputEvent& ev = out[count];
        ev.x = pos.x;
        ev.y = pos.y;

        if (toolMode == eSmudge) {
            ev.srcX = smudgeSrcPos.x;
            ev.srcY = smudgeSrcPos.y;
            smudgeSrcPos = pos;
        } else {
            ev.srcX = pos.x;
            ev.srcY = pos.y;
        }

        ev.brush = ib;
        count++;
    }

    // Update accumulators and tracking positions
    if (count > 0) {
        float lastDabDist = firstDist + (count - 1) * spacing;
        dabAccum = stdist - lastDabDist;
        lastDabPos = Vector2{from.x + lastDabDist * x2r, from.y + lastDabDist * y2r};
    } else {
        dabAccum += stdist;
    }
    prevInputPos = to;
    segBrushFrom.Realb = targetBrush;

    return count;
}
