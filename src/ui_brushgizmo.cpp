#include "repaint.h"
#include "rlgl.h"
#include "external/glad.h"   // raylib's glad — has GL_ constants, no windows.h
#include "imgui.h"
#include <math.h>

void BrushGizmo_Draw(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState* state) {
    float rang = state->currentBrush.Realb.resangle * (float)(M_PI * 2.0 / 360.0);
    int arrLen = 80;
    ImVec2 arrTip(gcx + arrLen * cosf(rang), gcy + arrLen * sinf(rang));
    dl->AddLine(org, arrTip, IM_COL32(200, 40, 40, 255), 3);
    float ah = (float)(M_PI * 0.2);
    ImVec2 a1(arrTip.x + 10 * cosf(rang - (float)M_PI + ah), arrTip.y + 10 * sinf(rang - (float)M_PI + ah));
    ImVec2 a2(arrTip.x + 10 * cosf(rang - (float)M_PI - ah), arrTip.y + 10 * sinf(rang - (float)M_PI - ah));
    dl->AddLine(arrTip, a1, IM_COL32(200, 40, 40, 255), 3);
    dl->AddLine(arrTip, a2, IM_COL32(200, 40, 40, 255), 3);
    dl->AddCircleFilled(org, 3, IM_COL32_BLACK);
    dl->AddCircleFilled(org, 2, IM_COL32_WHITE);
}

void BrushGizmo_DrawXOROverlay(AppState* state) {
    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    float d30 = (float)(M_PI * 30.0 / 180.0);

    rlDrawRenderBatchActive();

    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        float len = 220.0f;
        DrawLineEx(Vector2{(float)gcx, (float)gcy},
                   Vector2{(float)gcx + len * cosf(a), (float)gcy + len * sinf(a)},
                   3.0f, WHITE);
    }

    // Use base slider values for gizmo display (not velocity-modulated)
    float baseRadOut = BParam_GetValue(&bpSize);
    float baseHard   = BParam_GetValue(&bpHardness);
    float baseCrv    = BParam_GetValue(&bpCurvature);
    float baseZoom   = state->camera.zoom;

    Vector2 ctr = {(float)gcx, (float)gcy};
    float drawRadOut = baseRadOut * baseZoom;

    if (drawRadOut > 2.0f)
        DrawRing(ctr, drawRadOut - 1.5f, drawRadOut + 1.5f, 0, 360, 0, WHITE);

    float hardStart  = -GIZMO_HARD_ANG_START;
    float hardEnd    = hardStart - GIZMO_HARD_ANG_SPAN;
    float curveStart = -(GIZMO_HARD_ANG_START + GIZMO_HARD_ANG_SPAN);
    float curveEnd   = curveStart - GIZMO_CURVE_ANG_SPAN;

    DrawRing(ctr, GIZMO_FIXED_RADIUS_PX - 1.5f, GIZMO_FIXED_RADIUS_PX + 1.5f, hardEnd,  hardStart,  0, WHITE);
    DrawRing(ctr, GIZMO_FIXED_RADIUS_PX - 1.5f, GIZMO_FIXED_RADIUS_PX + 1.5f, curveEnd, curveStart, 0, WHITE);

    float hRatio = fminf(baseHard, 1.0f);
    float hardMv = GIZMO_FIXED_RADIUS_PX * hRatio;
    if (hardMv > 2.0f)
        DrawRing(ctr, hardMv - 1.5f, hardMv + 1.5f, hardEnd, hardStart, 0, WHITE);

    float curveMv = GIZMO_FIXED_RADIUS_PX * (1.0f - baseCrv);
    if (curveMv > 2.0f)
        DrawRing(ctr, curveMv - 1.5f, curveMv + 1.5f, curveEnd, curveStart, 0, WHITE);

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}
