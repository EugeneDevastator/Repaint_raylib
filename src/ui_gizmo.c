#include "repaint.h"

bool gizmoShow = false;
int gizmoMouseMode = 0;

void Gizmo_HandleInput(AppState* state, Vector2 mousePos) {
    int gcx = UI_PANEL_WIDTH + (RIGHT_PANEL_X - UI_PANEL_WIDTH) / 2;
    int gcy = SCREEN_HEIGHT / 2;
    float dx = mousePos.x - gcx;
    float dy = mousePos.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);
    float d30 = M_PI * 30 / 180.0f;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dist < 100 && dist > 3) {
        float angDeg = (ang + M_PI) * 180.0f / M_PI;
        float rotDiff = fabsf(angDeg - fmodf(state->currentBrush.Realb.resangle, 360.0f));
        if (rotDiff > 180.0f) rotDiff = 360.0f - rotDiff;
        if (rotDiff < 5.0f)
            gizmoMouseMode = 4;
        else if (ang > d30 && ang < d30 * 5)
            gizmoMouseMode = 1;
        else if (ang < d30 && ang > -M_PI * 0.5f)
            gizmoMouseMode = 2;
        else
            gizmoMouseMode = 3;
    }

    if (gizmoMouseMode > 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        float rad = dist / state->zoomK;
        float absrad = dist;
        int curMode = 0;
        if (ang > d30 && ang < d30 * 5) curMode = 1;
        else if (ang < d30 && ang > -M_PI * 0.5f) curMode = 2;
        else curMode = 3;

        if (gizmoMouseMode == 1) {
            float rel = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 1;
            if (curMode != 1)
                rad = roundf(rad / 10.0f) * 10.0f;
            float newRad = fmaxf(1.0f, rad);
            state->currentBrush.Realb.rad_out = newRad;
            state->currentBrush.Realb.rad_in = newRad * rel;
            if (state->currentBrush.Realb.rad_in > newRad * 0.98f)
                state->currentBrush.Realb.rad_in = newRad * 0.98f;
            BParam_SetValue(&bpSize, newRad);
            bpHardness.slider.clipmaxF = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
        }
        if (gizmoMouseMode == 2) {
            float newRadIn = fminf(rad, state->currentBrush.Realb.rad_out);
            if (curMode != 2) newRadIn = 0;
            if (newRadIn < 7) newRadIn = 0;
            if (newRadIn > state->currentBrush.Realb.rad_out * 0.98f)
                newRadIn = state->currentBrush.Realb.rad_out * 0.98f;
            state->currentBrush.Realb.rad_in = newRadIn;
            bpHardness.slider.clipmaxF = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
        }
        if (gizmoMouseMode == 3) {
            float ir = fminf(rad, state->currentBrush.Realb.rad_out);
            if (curMode != 3) ir = 0;
            state->currentBrush.Realb.crv = ir / fmaxf(1.0f, state->currentBrush.Realb.rad_out);
        }
        if (gizmoMouseMode == 4) {
            float newAng = (ang + M_PI) * 180.0f / M_PI;
            if (absrad > 160.0f || rad < 20.0f)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->currentBrush.Realb.resangle = newAng;
        }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) gizmoMouseMode = 0;
}

void Gizmo_Draw(AppState* state) {
    if (!gizmoShow) return;

    int gcx = UI_PANEL_WIDTH + (RIGHT_PANEL_X - UI_PANEL_WIDTH) / 2;
    int gcy = SCREEN_HEIGHT / 2;
    int gs = 200;
    int gizR = gs / 2;
    float drawRadOut = state->currentBrush.Realb.rad_out * state->zoomK;
    float drawRadIn = state->currentBrush.Realb.rad_in * state->zoomK;
    if (drawRadOut > gizR) drawRadOut = gizR;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;

    float d30 = M_PI * 30 / 180;
    Vector2 org = {(float)gcx, (float)gcy};

    int overlayW = 540;
    int overlayH = 480;
    DrawRectangle(gcx - overlayW / 2, gcy - gizR, overlayW, overlayH, (Color){0, 0, 0, 160});
    DrawRectangleLines(gcx - overlayW / 2, gcy - gizR, overlayW, overlayH, (Color){100, 100, 120, 255});

    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        Vector2 tip = {gcx + gizR * cosf(a), gcy + gizR * sinf(a)};
        DrawLineEx(org, tip, 3, BLACK);
        DrawLineEx(org, tip, 1, WHITE);
    }

    DrawCircleLines(gcx, gcy, drawRadOut, BLACK);
    DrawCircleLines(gcx, gcy, drawRadOut + 1, WHITE);
    DrawCircleLines(gcx, gcy, drawRadOut - 1, WHITE);

    if (drawRadIn > 0) {
        Vector2 c = {(float)gcx, (float)gcy};
        DrawCircleSectorLines(c, drawRadIn, 90, 210, 0, BLACK);
        DrawCircleSectorLines(c, drawRadIn + 1, 90, 210, 0, WHITE);
        DrawCircleSectorLines(c, drawRadIn - 1, 90, 210, 0, WHITE);
    }

    float drawRelRad = drawRadOut * state->currentBrush.Realb.crv;
    if (drawRelRad > 1) {
        Vector2 c = {(float)gcx, (float)gcy};
        DrawCircleSectorLines(c, drawRelRad, 330, 450, 0, BLACK);
        DrawCircleSectorLines(c, drawRelRad + 1, 330, 450, 0, WHITE);
        DrawCircleSectorLines(c, drawRelRad - 1, 330, 450, 0, WHITE);
    }

    float rang = state->currentBrush.Realb.resangle * M_PI * 2 / 360;
    int arrLen = 80;
    Vector2 arrTip = {gcx + arrLen * cosf(rang), gcy + arrLen * sinf(rang)};
    DrawLineEx(org, arrTip, 3, BLACK);
    DrawLineEx(org, arrTip, 1, WHITE);
    float ah = M_PI * 0.2f;
    Vector2 a1 = {arrTip.x + 10 * cosf(rang - M_PI + ah), arrTip.y + 10 * sinf(rang - M_PI + ah)};
    Vector2 a2 = {arrTip.x + 10 * cosf(rang - M_PI - ah), arrTip.y + 10 * sinf(rang - M_PI - ah)};
    DrawLineEx(arrTip, a1, 3, BLACK);
    DrawLineEx(arrTip, a2, 3, BLACK);
    DrawLineEx(arrTip, a1, 1, WHITE);
    DrawLineEx(arrTip, a2, 1, WHITE);

    DrawCircle(gcx, gcy, 3, BLACK);
    DrawCircle(gcx, gcy, 2, WHITE);

    char buf[64];
    sprintf(buf, "Size: %.0f  Hardness: %.0f%%", state->currentBrush.Realb.rad_out,
            (1.0f - state->currentBrush.Realb.rad_in / fmaxf(1, state->currentBrush.Realb.rad_out)) * 100);
    int tw = MeasureText(buf, 12);
    DrawText(buf, gcx - tw / 2, gcy + gizR + 8, 12, LIGHTGRAY);

    int swY = gcy + gizR + 14;
    Rectangle swRect = {(float)gcx - 256, (float)swY, 512, 24};
    Color curCol = HSLToRGB(colorHue, colorSat, colorLit);
    DrawRectangleRec(swRect, curCol);
    DrawRectangleLinesEx(swRect, 1, WHITE);

    Rectangle hueR = {(float)gcx - 256, (float)(swY + 34), 512, 24};
    DrawColorGradientAt(hueR, 0, colorHue);
    UISlider_Draw(&sliderHue);

    Rectangle satR = {(float)gcx - 256, (float)(swY + 66), 512, 24};
    DrawColorGradientAt(satR, 1, colorHue);
    UISlider_Draw(&sliderSat);

    Rectangle litR = {(float)gcx - 256, (float)(swY + 98), 512, 24};
    DrawColorGradientAt(litR, 2, colorHue);
    UISlider_Draw(&sliderLit);
}
