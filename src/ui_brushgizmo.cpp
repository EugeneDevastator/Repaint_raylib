#include "repaint.h"
#include "rlgl.h"
#include "imgui.h"
#include <math.h>

#define GIZMO_CTRL_SZ 24

void DrawSliderVertical(ImDrawList* dl, BParam* bp, int x, int y, int w, int h, float val, int colorMode) {
    DualSlider* ds = &bp->slider;
    int y0 = y, y1 = y + h;
    for (int yy = y0; yy < y1; yy++) {
        float t = (float)(y1 - 1 - yy) / (float)(y1 - y0 - 1);
        ImU32 col;
        if (colorMode >= 0) {
            Color c;
            if (colorMode == 0) c = HSLToRGB(t, 1.0f, 0.5f);
            else if (colorMode == 1) c = HSLToRGB(colorHue, t, colorLit);
            else c = HSLToRGB(colorHue, colorSat, t);
            col = IM_COL32(c.r, c.g, c.b, 255);
        } else {
            uint8_t r = (uint8_t)(ds->gradStart.r * (1 - t) + ds->gradEnd.r * t);
            uint8_t g = (uint8_t)(ds->gradStart.g * (1 - t) + ds->gradEnd.g * t);
            uint8_t b = (uint8_t)(ds->gradStart.b * (1 - t) + ds->gradEnd.b * t);
            col = IM_COL32(r, g, b, 255);
        }
        dl->AddRectFilled(ImVec2(x, yy), ImVec2(x + w, yy + 1), col);
    }
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(180, 180, 200, 180));
    float grabY = y + (1.0f - val) * h;
    float grabHalf = GIZMO_CTRL_SZ * 0.25f;
    dl->AddRectFilled(ImVec2(x + 1, grabY - grabHalf), ImVec2(x + w - 1, grabY + grabHalf), IM_COL32_WHITE);
    dl->AddRect(ImVec2(x + 1, grabY - grabHalf), ImVec2(x + w - 1, grabY + grabHalf), IM_COL32(50, 50, 50, 200));
    char txt[16];
    float disp = BParam_GetValue(bp);
    if (bp->outMax - bp->outMin >= 1.0f) snprintf(txt, sizeof(txt), "%.1f", disp);
    else snprintf(txt, sizeof(txt), "%.2f", disp);
    ImVec2 tsz = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(x + (w - tsz.x) / 2, grabY - tsz.y / 2), IM_COL32(255, 255, 255, 220), txt);
}

void BrushGizmo_Draw(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState* state) {
    // ── Rotation arrow ──────────────────────────────────────────────────
    float rang = state->currentBrush.Realb.resangle * (float)(M_PI * 2.0 / 360.0);
    int arrLen = 80;
    ImVec2 arrTip(gcx + arrLen * cosf(rang), gcy + arrLen * sinf(rang));
    dl->AddLine(org, arrTip, IM_COL32(200, 40, 40, 255), 3);
    float ah = (float)(M_PI * 0.2);
    ImVec2 a1(arrTip.x + 10 * cosf(rang - (float)M_PI + ah), arrTip.y + 10 * sinf(rang - (float)M_PI + ah));
    ImVec2 a2(arrTip.x + 10 * cosf(rang - (float)M_PI - ah), arrTip.y + 10 * sinf(rang - (float)M_PI - ah));
    dl->AddLine(arrTip, a1, IM_COL32(200, 40, 40, 255), 3);
    dl->AddLine(arrTip, a2, IM_COL32(200, 40, 40, 255), 3);

    // ── Center dot ──────────────────────────────────────────────────────
    dl->AddCircleFilled(org, 3, IM_COL32_BLACK);
    dl->AddCircleFilled(org, 2, IM_COL32_WHITE);
}
void Gizmo_DrawXOROverlay(AppState* state) {
    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    float d30 = (float)(M_PI * 30.0 / 180.0);

    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ZERO, RL_FUNC_ADD);

    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        float len = 220.0f;
        DrawLineEx(Vector2{(float)gcx, (float)gcy},
                   Vector2{(float)gcx + len * cosf(a), (float)gcy + len * sinf(a)},
                   3.0f, WHITE);
    }

    Vector2 ctr = {(float)gcx, (float)gcy};
    float drawRadOut = state->currentBrush.Realb.rad_out * state->camera.zoom;

    if (drawRadOut > 2.0f) {
        DrawRing(ctr, drawRadOut - 1.5f, drawRadOut + 1.5f, 0, 360, 0, WHITE);
    }

    float hardStart  = -GIZMO_HARD_ANG_START;
    float hardEnd    = hardStart - GIZMO_HARD_ANG_SPAN;
    float curveStart = -( GIZMO_HARD_ANG_START + GIZMO_HARD_ANG_SPAN );
    float curveEnd   = curveStart - GIZMO_CURVE_ANG_SPAN;

    DrawRing(ctr, GIZMO_FIXED_RADIUS_PX - 1.5f, GIZMO_FIXED_RADIUS_PX + 1.5f, hardEnd,   hardStart,   0, WHITE);
    DrawRing(ctr, GIZMO_FIXED_RADIUS_PX - 1.5f, GIZMO_FIXED_RADIUS_PX + 1.5f, curveEnd,  curveStart,  0, WHITE);

    float hRatio = (state->currentBrush.Realb.rad_out > 0)
        ? fminf(state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out, 1.0f)
        : 0.0f;
    float hardMv = GIZMO_FIXED_RADIUS_PX * hRatio;
    if (hardMv > 2.0f) {
        DrawRing(ctr, hardMv - 1.5f, hardMv + 1.5f, hardEnd, hardStart, 0, WHITE);
    }

    float curveMv = GIZMO_FIXED_RADIUS_PX * (1.0f - state->currentBrush.Realb.crv);
    if (curveMv > 2.0f) {
        DrawRing(ctr, curveMv - 1.5f, curveMv + 1.5f, curveEnd, curveStart, 0, WHITE);
    }

    rlSetBlendMode(RL_BLEND_ALPHA);
}
