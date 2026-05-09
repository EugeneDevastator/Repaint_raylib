#include "repaint.h"

bool gizmoShow = false;
int gizmoMouseMode = 0;

#define GIZMO_TOOL_N 6
static const char* gizmoToolLabels[GIZMO_TOOL_N] = {"Br","Sm","Li","Er","Di","Co"};
static const int gizmoToolModes[GIZMO_TOOL_N] = {eBrush, eSmudge, eLine, -1, eDisp, eCont};

#define GIZMO_SLIDER_W 30
#define GIZMO_SLIDER_H 150
#define GIZMO_SLIDER_GAP 8
#define GIZMO_THUMB_H 8

void Gizmo_HandleInput(AppState* state, Vector2 mousePos) {
    int gcx = uiPanelWidth + (RIGHT_PANEL_X - uiPanelWidth) / 2;
    int gcy = SCREEN_HEIGHT / 2;
    int gs = 200;
    int gizR = gs / 2;
    float dx = mousePos.x - gcx;
    float dy = mousePos.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);
    float d30 = M_PI * 30 / 180.0f;

    // Radial gizmo input
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
        float rad = dist / state->camera.zoom;
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
            float h = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
            BParam_SetValue(&bpHardness, h);
        }
        if (gizmoMouseMode == 2) {
            float newRadIn = fminf(rad, state->currentBrush.Realb.rad_out);
            if (curMode != 2) newRadIn = 0;
            if (newRadIn < 7) newRadIn = 0;
            if (newRadIn > state->currentBrush.Realb.rad_out * 0.98f)
                newRadIn = state->currentBrush.Realb.rad_out * 0.98f;
            state->currentBrush.Realb.rad_in = newRadIn;
            float h = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
            BParam_SetValue(&bpHardness, h);
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

    // Tool buttons input
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        int bx = gcx - 256;
        int by = gcy + gizR + 14 + 24 + 8;
        for (int i = 0; i < GIZMO_TOOL_N; i++) {
            Rectangle r = {(float)(bx + i * 44), (float)by, 40, 28};
            if (CheckCollisionPointRec(mousePos, r)) {
                if (i == 3) {
                    if (state->mode == eBrush && state->currentBrush.Realb.col.a == 0) {
                        state->mode = eBrush;
                        state->currentBrush.Realb.col.a = 255;
                    } else {
                        state->mode = eBrush;
                        state->currentBrush.Realb.col.a = 0;
                    }
                } else {
                    state->mode = gizmoToolModes[i];
                    state->currentBrush.Realb.col.a = 255;
                }
                return;
            }
        }
    }

    // Color sliders input
    {
        static bool drag = false;
        static int dragS = -1;
        int slX = gcx + 120;
        int slY = gcy - 75;

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            for (int s = 0; s < 3; s++) {
                Rectangle r = {(float)(slX + s * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP)), (float)slY, (float)GIZMO_SLIDER_W, (float)GIZMO_SLIDER_H};
                if (CheckCollisionPointRec(mousePos, r)) {
                    drag = true;
                    dragS = s;
                    break;
                }
            }
        }

        if (drag && dragS >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            float t = (mousePos.y - slY) / (float)GIZMO_SLIDER_H;
            t = fminf(1.0f, fmaxf(0.0f, t));
            switch (dragS) {
                case 0: colorHue = t; break;
                case 1: colorSat = t; break;
                case 2: colorLit = t; break;
            }
        }

        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            drag = false;
            dragS = -1;
        }
    }
}

void Gizmo_Draw(AppState* state) {
    if (!gizmoShow) return;

    int gcx = uiPanelWidth + (RIGHT_PANEL_X - uiPanelWidth) / 2;
    int gcy = SCREEN_HEIGHT / 2;
    int gs = 200;
    int gizR = gs / 2;
    float drawRadOut = state->currentBrush.Realb.rad_out * state->camera.zoom;
    float drawRadIn = state->currentBrush.Realb.rad_in * state->camera.zoom;
    if (drawRadOut > gizR) drawRadOut = gizR;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;

    float d30 = M_PI * 30 / 180;
    Vector2 org = {(float)gcx, (float)gcy};

    int overlayW = 540;
    int overlayH = 480;
    int overlayY = gcy - gizR;
    DrawRectangle(gcx - overlayW / 2, overlayY, overlayW, overlayH, (Color){0, 0, 0, 160});
    DrawRectangleLines(gcx - overlayW / 2, overlayY, overlayW, overlayH, (Color){100, 100, 120, 255});

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

    // Tool buttons row
    int tbY = swY + 24 + 8;
    for (int i = 0; i < GIZMO_TOOL_N; i++) {
        Rectangle r = {(float)(gcx - 256 + i * 44), (float)tbY, 40, 28};
        bool active;
        if (i == 3)
            active = (state->mode == eBrush && state->currentBrush.Realb.col.a == 0);
        else
            active = (state->mode == gizmoToolModes[i]);
        if (active) {
            DrawRectangleRec(r, (Color){50, 110, 190, 200});
        }
        DrawRectangleLinesEx(r, 1, (Color){180, 180, 200, 200});
        DrawText(gizmoToolLabels[i], (int)r.x + 8, (int)r.y + 6, 14, (Color){220, 220, 230, 230});
    }

    // Color sliders — vertical bars on the right side
    {
        int slX = gcx + 120;
        int slY = gcy - 75;
        float vals[3] = {colorHue, colorSat, colorLit};
        for (int s = 0; s < 3; s++) {
            Rectangle r = {(float)(slX + s * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP)), (float)slY, (float)GIZMO_SLIDER_W, (float)GIZMO_SLIDER_H};
            int y0 = (int)r.y, y1 = (int)(r.y + r.height);
            for (int y = y0; y < y1; y++) {
                float t = (float)(y - y0) / (float)(y1 - y0 - 1);
                Color c;
                switch (s) {
                    case 0: c = HSLToRGB(t, 1.0f, 0.5f); break;
                    case 1: c = HSLToRGB(colorHue, t, colorLit); break;
                    case 2: c = HSLToRGB(colorHue, colorSat, t); break;
                    default: c = BLACK;
                }
                DrawRectangle((int)r.x, y, (int)r.width, 1, c);
            }
            DrawRectangleLinesEx(r, 1, (Color){180, 180, 200, 180});
            float thumbY = r.y + vals[s] * r.height;
            DrawRectangle((int)r.x - 1, (int)(thumbY - GIZMO_THUMB_H / 2), (int)r.width + 2, GIZMO_THUMB_H, WHITE);
            DrawRectangleLines((int)r.x - 1, (int)(thumbY - GIZMO_THUMB_H / 2), (int)r.width + 2, GIZMO_THUMB_H, (Color){50, 50, 50, 200});
        }
    }
}
