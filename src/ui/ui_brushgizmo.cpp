#include "repaint.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"
#include <math.h>

#define GIZMO_FIXED_RADIUS_PX  180.0f
#define GIZMO_RADOUT_START   30.0f
#define GIZMO_RADOUT_ANG_SPAN    120.0f
#define GIZMO_HARD_ANG_START   150.0f
#define GIZMO_HARD_ANG_SPAN    120.0f
#define GIZMO_CURVE_ANG_SPAN   120.0f
#define ARROW_DRAW_RADIUS   200.0f
#define ARROW_ARM_LENGTH    15.0f
#define ARROW_SNAP_RADIUS   60.0f
#define ARROW_MIN_RADIUS    30.0f
#define ARROW_DETECT_PX     5.0f

void XORgizmo_DrawVisual(AppState* state) {
    if (g_activeHud != HUD_QUICK) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    float d30 = (float)(M_PI * 30.0 / 180.0);

    float baseSz   = BParam_GetValue(&bpSize);
    float baseHard = BParam_GetValue(&bpHardness);
    float baseCrv  = BParam_GetValue(&bpCurvature);
    Vector2 ctr = {(float)gcx, (float)gcy};

    // ── XOR overlay ────────────────────────────────────────────────
    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        DrawLineEx(Vector2{(float)gcx, (float)gcy},
                   Vector2{(float)gcx + 220.0f * cosf(a), (float)gcy + 220.0f * sinf(a)},
                   3.0f, WHITE);
    }

    // Brush size ring — now arc only in RADOUT sector
    float drawRadOut = baseSz * state->camera.zoom;
    if (drawRadOut > 2.0f)
        DrawRing(ctr, drawRadOut - 1.5f, drawRadOut + 1.5f,
                 -GIZMO_RADOUT_START, -GIZMO_RADOUT_START - GIZMO_RADOUT_ANG_SPAN, 0, WHITE);

    // Reference arc for radout sector
    float refR = GIZMO_FIXED_RADIUS_PX + 3.0f;
    //DrawRing(ctr, refR - 1.5f, refR + 1.5f,
    //         -GIZMO_RADOUT_START, -GIZMO_RADOUT_START - GIZMO_RADOUT_ANG_SPAN, 0, WHITE);
    float lineThick = 1.0f;
    // Hardness reference arc — RESTORED original
    float hardStart = -GIZMO_HARD_ANG_START;
    float hardEnd   = hardStart - GIZMO_HARD_ANG_SPAN;
    float curveStart = -(GIZMO_HARD_ANG_START + GIZMO_HARD_ANG_SPAN);
    float curveEnd   = curveStart - GIZMO_CURVE_ANG_SPAN;
    DrawRing(ctr, refR - lineThick, refR + lineThick, hardEnd,  hardStart,  0, WHITE);
    DrawRing(ctr, refR - lineThick, refR + lineThick, curveEnd, curveStart, 0, WHITE);

    // Hardness value arc — RESTORED original
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

    // ── Arrow: RED, outside XOR ─────────────────────────────────────
    {
        float rang = state->currentBrush.Realb.resangle * (float)(M_PI * 2.0 / 360.0);
        float ax = gcx + ARROW_DRAW_RADIUS * cosf(rang);
        float ay = gcy + ARROW_DRAW_RADIUS * sinf(rang);
        float ah  = (float)(M_PI * 0.2);
        float arm = ARROW_ARM_LENGTH;
        DrawLineEx(Vector2{(float)gcx, (float)gcy}, Vector2{ax, ay}, 3.0f, RED);
        DrawLineEx(Vector2{ax, ay},
            Vector2{ax + arm * cosf(rang - (float)M_PI + ah), ay + arm * sinf(rang - (float)M_PI + ah)},
            3.0f, RED);
        DrawLineEx(Vector2{ax, ay},
            Vector2{ax + arm * cosf(rang - (float)M_PI - ah), ay + arm * sinf(rang - (float)M_PI - ah)},
            3.0f, RED);
        DrawCircle(gcx, gcy, 3, RED);
        DrawCircle(gcx, gcy, 2, BLACK);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}

void XORgizmo_HandleInput(AppState* state) {
    if (g_activeHud != HUD_QUICK) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    float d30 = (float)(M_PI * 30.0 / 180.0);

    // Clip window to area above the texture panel so clicks in the tex
    // area fall through to the ##qpui window below.
    float gizmoH = vp.height;
    if (g_texPanelAreaY > 0 && g_texPanelAreaY > vp.y)
        gizmoH = g_texPanelAreaY - vp.y;

    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, gizmoH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##xorgizmo", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - gcx, dy = mp.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang  = AtanXY(dx, dy);
    bool down     = ImGui::IsMouseDown(0);
    bool clicked  = ImGui::IsMouseClicked(0);
    bool released = ImGui::IsMouseReleased(0);

    if (clicked && !ImGui::IsAnyItemHovered()) {
        // Clip to gizmo window — ignore clicks in the texture panel area
        if (mp.y >= vp.y + gizmoH) { ImGui::End(); return; }
        // Arrow priority: perpendicular distance to arrow line
        float arrowAng = state->currentBrush.Realb.resangle * (float)(M_PI * 2.0 / 360.0);
        float perpDist = fabsf(dx * sinf(arrowAng) - dy * cosf(arrowAng));
        float along    = dx * cosf(arrowAng) + dy * sinf(arrowAng);
        bool arrowHit  = (perpDist <= ARROW_DETECT_PX) && (along >= ARROW_MIN_RADIUS);

        if (arrowHit) {
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
        float rad    = dist / state->camera.zoom;
        float absrad = dist;
        int curMode = 0;
        if (ang > d30 && ang < d30 * 5) curMode = 1;
        else if (ang < d30 && ang > -(float)M_PI * 0.5f) curMode = 2;
        else curMode = 3;

        if (quickPanelMouseMode == 1) {
            float rel = fminf(state->currentBrush.Realb.radInRatio, 1.0f);
            if (curMode != 1) rad = roundf(rad / 10.0f) * 10.0f;
            float newRad = fmaxf(1.0f, rad);
            state->currentBrush.Realb.rad_out = newRad;
            state->currentBrush.Realb.radInRatio = rel;
            BParam_SetValue(&bpSize, newRad);
            BParam_SetValue(&bpHardness, rel);
        }
        if (quickPanelMouseMode == 2) {
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 2) h = 0.0f;
            if (h < 0.05f) h = 0.0f;
            state->currentBrush.Realb.radInRatio = h;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 3) {
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 3) h = 0.0f;
            state->currentBrush.Realb.crv = 1.0f - h;
            BParam_SetValue(&bpCurvature, 1.0f - h);
        }
        if (quickPanelMouseMode == 4) {
            float newAng = atan2f(dy, dx) * 180.0f / (float)M_PI;
            if (newAng < 0) newAng += 360.0f;
            if (absrad < ARROW_SNAP_RADIUS)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->currentBrush.Realb.resangle = newAng;
            state->initialAngle = newAng;
        }
    }

    if (released) quickPanelMouseMode = 0;
    ImGui::End();
}
