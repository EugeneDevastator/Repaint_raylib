#include "repaint.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"
#include <math.h>

#define ARROW_DRAW_RADIUS   180.0f
#define ARROW_ARM_LENGTH    10.0f
#define ARROW_SNAP_RADIUS   60.0f
#define ARROW_MIN_RADIUS    30.0f

static void DrawArrow(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState* state) {
    float rang = state->currentBrush.Realb.resangle * (float)(M_PI * 2.0 / 360.0);
    ImVec2 arrTip(gcx + ARROW_DRAW_RADIUS * cosf(rang), gcy + ARROW_DRAW_RADIUS * sinf(rang));
    dl->AddLine(org, arrTip, IM_COL32(200, 40, 40, 255), 3);

    float ah = (float)(M_PI * 0.2);
    float arm = ARROW_ARM_LENGTH;
    ImVec2 a1(arrTip.x + arm * cosf(rang - (float)M_PI + ah), arrTip.y + arm * sinf(rang - (float)M_PI + ah));
    ImVec2 a2(arrTip.x + arm * cosf(rang - (float)M_PI - ah), arrTip.y + arm * sinf(rang - (float)M_PI - ah));
    dl->AddLine(arrTip, a1, IM_COL32(200, 40, 40, 255), 3);
    dl->AddLine(arrTip, a2, IM_COL32(200, 40, 40, 255), 3);
    dl->AddCircleFilled(org, 3, IM_COL32_BLACK);
    dl->AddCircleFilled(org, 2, IM_COL32_WHITE);
}

void XORgizmo_Draw(AppState* state) {
    if (!quickPanelShow || !g_showBrushPreview) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    float d30 = (float)(M_PI * 30.0 / 180.0);

    // ── XOR overlay (raylib, before ImGui window) ──────────────────
    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        DrawLineEx(Vector2{(float)gcx, (float)gcy},
                   Vector2{(float)gcx + 220.0f * cosf(a), (float)gcy + 220.0f * sinf(a)},
                   3.0f, WHITE);
    }

    float baseRadOut = BParam_GetValue(&bpSize);
    float baseHard   = BParam_GetValue(&bpHardness);
    float baseCrv    = BParam_GetValue(&bpCurvature);
    Vector2 ctr = {(float)gcx, (float)gcy};
    float drawRadOut = baseRadOut * state->camera.zoom;

    if (drawRadOut > 2.0f)
        DrawRing(ctr, drawRadOut - 1.5f, drawRadOut + 1.5f, 0, 360, 0, WHITE);

    float hardStart  = -GIZMO_HARD_ANG_START;
    float hardEnd    = hardStart - GIZMO_HARD_ANG_SPAN;
    float curveStart = -(GIZMO_HARD_ANG_START + GIZMO_HARD_ANG_SPAN);
    float curveEnd   = curveStart - GIZMO_CURVE_ANG_SPAN;

    float refR = GIZMO_FIXED_RADIUS_PX + 3.0f;
    DrawRing(ctr, refR - 1.5f, refR + 1.5f, hardEnd,  hardStart,  0, WHITE);
    DrawRing(ctr, refR - 1.5f, refR + 1.5f, curveEnd, curveStart, 0, WHITE);

    float hRatio = fminf(baseHard, 1.0f);
    float hardMv = GIZMO_FIXED_RADIUS_PX * hRatio;
    if (hardMv > 2.0f)
        DrawRing(ctr, hardMv - 1.5f, hardMv + 1.5f, hardEnd, hardStart, 0, WHITE);

    float curvRatio = fminf(fmaxf(1.0f - baseCrv, 0.0f), 1.0f);
    float curveMv = GIZMO_FIXED_RADIUS_PX * curvRatio;
    if (curveMv > 2.0f)
        DrawRing(ctr, curveMv - 1.5f, curveMv + 1.5f, curveEnd, curveStart, 0, WHITE);

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);

    // ── ImGui overlay window (arrow + radial input) ───────────────
    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, vp.height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##xorgizmo", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawArrow(dl, ImVec2((float)gcx, (float)gcy), gcx, gcy, state);

    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - gcx, dy = mp.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);
    bool down = ImGui::IsMouseDown(0);
    bool clicked = ImGui::IsMouseClicked(0);
    bool released = ImGui::IsMouseReleased(0);

    if (clicked && !ImGui::IsAnyItemHovered()) {
        float arrowAng = state->initialAngle * (float)(M_PI * 2.0 / 360.0);
        float angDiff = fabsf(ang - arrowAng);
        if (angDiff > (float)M_PI) angDiff = (float)(2.0f * M_PI) - angDiff;

        if (angDiff < 20.0f * (float)M_PI / 180.0f) {
            quickPanelMouseMode = 4;
        } else {
            bool inSector1 = (ang > d30 && ang < d30 * 5);
            bool inSector2 = (ang < d30 && ang > -(float)M_PI * 0.5f);

            if (dist <= GIZMO_FIXED_RADIUS_PX) {
                if (inSector1) quickPanelMouseMode = 1;
                else if (inSector2) quickPanelMouseMode = 2;
                else quickPanelMouseMode = 3;
            } else {
                quickPanelMouseMode = inSector1 ? 1 : 4;
            }
        }
    }

    if (quickPanelMouseMode > 0 && down) {
        float rad = dist / state->camera.zoom;
        float absrad = dist;
        int curMode = 0;
        if (ang > d30 && ang < d30 * 5) curMode = 1;
        else if (ang < d30 && ang > -(float)M_PI * 0.5f) curMode = 2;
        else curMode = 3;

        if (quickPanelMouseMode == 1) {
            float rel = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 1;
            if (curMode != 1) rad = roundf(rad / 10.0f) * 10.0f;
            float newRad = fmaxf(1.0f, rad);
            state->currentBrush.Realb.rad_out = newRad;
            state->currentBrush.Realb.rad_in = newRad * rel;
            BParam_SetValue(&bpSize, newRad);
            float h = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 2) {
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 2) h = 0.0f;
            if (h < 0.05f) h = 0.0f;
            state->currentBrush.Realb.rad_in = h * state->currentBrush.Realb.rad_out;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 3) {
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 3) h = 0.0f;
            state->currentBrush.Realb.crv = 1.0f - h;
            BParam_SetValue(&bpCurvature, 1.0f - h);
        }
        if (quickPanelMouseMode == 4) {
            float newAng = (ang + (float)M_PI) * 180.0f / (float)M_PI;
            if (absrad > ARROW_SNAP_RADIUS || rad < ARROW_MIN_RADIUS)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->initialAngle = newAng;
        }
    }

    if (released) quickPanelMouseMode = 0;
    ImGui::End();
}
