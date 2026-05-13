#include "repaint.h"
#include "stroke.h"
#include <math.h>
#include <string.h>

BrushInterpolator::BrushInterpolator() {
    memset(&segBrushFrom, 0, sizeof(segBrushFrom));
    lastDabPos = Vector2{0, 0};
    smudgeSrcPos = Vector2{0, 0};
    dabAccum = 0.0f;
    inStroke = false;
}

void BrushInterpolator::BeginStroke(const d_Brush& userBrush, float startX, float startY) {
    memset(&segBrushFrom, 0, sizeof(segBrushFrom));
    segBrushFrom = userBrush;
    lastDabPos = Vector2{startX, startY};
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
    float spacingVal, int toolMode)
{
    if (!inStroke || maxOut <= 0) return 0;

    Vector2 from = lastDabPos;
    Vector2 to = {pt.x, pt.y};
    float stdist = Dist2D(from, to);
    if (stdist < 0.001f) return 0;

    // Qt-style spacing: fixed from segment-start brush
    float spacing = fmaxf(segBrushFrom.Realb.rad_out * spacingVal, 1.0f);

    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float x2r = dx / stdist;
    float y2r = dy / stdist;

    float tdist = stdist + dabAccum;
    if (tdist < spacing) {
        dabAccum += stdist;
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

        // Brush interpolation: k=0 at segment start, k=1 at end (Qt)
        float k = fminf(d / fmaxf(stdist, 0.001f), 1.0f);
        d_RealBrush ib = Stroke_BlendBrushes(segBrushFrom.Realb, targetBrush, k);

        // Build InputEvent with the interpolated brush
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

    // Chain last dab position (Qt CalcLastPos)
    float lastDabDist = firstDist + extraDabs * spacing;
    dabAccum = stdist - lastDabDist;
    lastDabPos = Vector2{from.x + lastDabDist * x2r, from.y + lastDabDist * y2r};
    segBrushFrom.Realb = targetBrush;

    return count;
}
