#include "repaint.h"
#include "imgui.h"
#include <math.h>

bool gizmoShow = false;
int gizmoMouseMode = 0;

#define GIZMO_TOOL_N 6
static const char* gizmoToolLabels[GIZMO_TOOL_N] = {"Br","Sm","Li","Er","Di","Co"};
static const int gizmoToolModes[GIZMO_TOOL_N] = {eBrush, eSmudge, eLine, -1, eDisp, eCont};

#define GIZMO_SLIDER_W 48
#define GIZMO_SLIDER_H 256
#define GIZMO_SLIDER_GAP 8
#define GIZMO_PENBTN_H 24
#define GIZMO_ICON_H 24
#define GIZMO_THUMB_W 6

#define FILE_BTN_N 7
static const char* fileBtnLabels[FILE_BTN_N] = {"New","Open","SaveAs","Save","Reload","Snap","Pin"};
static Texture2D fileBtnTex[FILE_BTN_N];

void LoadGizmoIcons(void) {
    const char* names[FILE_BTN_N] = {"btnnew","btnopen","btnsaveas","btnsave","btnreload","btnsnap","btnpin"};
    for (int i = 0; i < FILE_BTN_N; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", names[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 24, 24);
            fileBtnTex[i] = LoadTextureFromImage(img);
            UnloadImage(img);
        } else {
            fileBtnTex[i] = Texture2D{0};
        }
    }
}

void UnloadGizmoIcons(void) {
    for (int i = 0; i < FILE_BTN_N; i++) {
        if (fileBtnTex[i].id > 0) UnloadTexture(fileBtnTex[i]);
    }
}

static Rectangle VpRect(void) { return viewport.bounds; }
static int GizmoCX(void) { Rectangle vp = VpRect(); return (int)(vp.x + vp.width * 0.5f); }
static int GizmoCY(void) { Rectangle vp = VpRect(); return (int)(vp.y + vp.height * 0.5f); }

void Gizmo_HandleInput(AppState*, Vector2) {}

static void DrawSliderVertical(ImDrawList* dl, BParam* bp, int x, int y, int w, int h, float val, int colorMode) {
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
    dl->AddRectFilled(ImVec2(x + 1, grabY - GIZMO_THUMB_W / 2), ImVec2(x + w - 1, grabY + GIZMO_THUMB_W / 2), IM_COL32_WHITE);
    dl->AddRect(ImVec2(x + 1, grabY - GIZMO_THUMB_W / 2), ImVec2(x + w - 1, grabY + GIZMO_THUMB_W / 2), IM_COL32(50, 50, 50, 200));
    char txt[16];
    float disp = BParam_GetValue(bp);
    if (bp->outMax - bp->outMin >= 1.0f) snprintf(txt, sizeof(txt), "%.1f", disp);
    else snprintf(txt, sizeof(txt), "%.2f", disp);
    ImVec2 tsz = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(x + (w - tsz.x) / 2, grabY - tsz.y / 2), IM_COL32(255, 255, 255, 220), txt);
}

void Gizmo_Draw(AppState* state) {
    if (!gizmoShow) return;

    Rectangle vp = VpRect();
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);
    int gizR = 200;
    float d30 = (float)(M_PI * 30.0 / 180.0);

    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, vp.height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##gizmo", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 org((float)gcx, (float)gcy);

    float drawRadOut = state->currentBrush.Realb.rad_out * state->camera.zoom;
    float drawRadIn = state->currentBrush.Realb.rad_in * state->camera.zoom;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;
	float drawRelRad = drawRadOut * (1.0f - state->currentBrush.Realb.crv);


    // ── Radial guide lines ──────────────────────────────────────────────
    for (int gi = 1; gi <= 5; gi += 2) {
        float a = -d30 * (2 * gi - 1);
        ImVec2 tip(gcx + gizR * cosf(a), gcy + gizR * sinf(a));
        dl->AddLine(org, tip, IM_COL32_BLACK, 3);
        dl->AddLine(org, tip, IM_COL32_WHITE, 1);
    }

    // ── Gizmo circles ───────────────────────────────────────────────────
    dl->AddCircle(org, drawRadOut,     IM_COL32_BLACK,  0, 3);
    dl->AddCircle(org, drawRadOut + 1, IM_COL32_WHITE,  0, 2);
    dl->AddCircle(org, drawRadOut - 1, IM_COL32_WHITE,  0, 2);

    // ── 3 × 120° sector arcs ────────────────────────────────────────────
    // Rays at -30°, -90°, -150° → sectors:
    //   [-150° → -30°]  top      = size      (drawRadOut)
    //   [-30°  →  90°]  bot-right = curvature (drawRelRad)
    //   [ 90°  → 210°]  bot-left  = hardness  (drawRadIn)
    {
        float deg = (float)(M_PI / 180.0);
        // sector start angles, CCW in screen space (Y-down = CW visually)
        float starts[3] = { -150.0f * deg, -30.0f * deg,  90.0f * deg };
        float ends[3]   = {  -30.0f * deg,  90.0f * deg, 210.0f * deg };
        float rads[3]   = { drawRadOut, drawRelRad, drawRadIn };

        for (int i = 0; i < 3; i++) {
            float rad = rads[i];
            if (rad <= 0) continue;
            float a0 = starts[i];
            float a1 = ends[i];
            dl->PathClear();
            dl->PathArcTo(org, rad,     a0, a1, 0);
            dl->PathStroke(IM_COL32_BLACK, false, 3.0f);
            dl->PathClear();
            dl->PathArcTo(org, rad + 1, a0, a1, 0);
            dl->PathStroke(IM_COL32_WHITE, false, 2.0f);
            dl->PathClear();
            dl->PathArcTo(org, rad - 1, a0, a1, 0);
            dl->PathStroke(IM_COL32_WHITE, false, 2.0f);
        }
    }


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

    dl->AddCircleFilled(org, 3, IM_COL32_BLACK);
    dl->AddCircleFilled(org, 2, IM_COL32_WHITE);

    // ── Info text ───────────────────────────────────────────────────────
    char buf[96];
    float hardness = 1.0f - state->currentBrush.Realb.rad_in / fmaxf(1.0f, state->currentBrush.Realb.rad_out);
    snprintf(buf, sizeof(buf), "Sz:%.0f Hd:%.0f%% Op:%.0f%% Crv:%.0f%% Sp:%.2f Sc:%.2f",
        state->currentBrush.Realb.rad_out, hardness * 100,
        state->currentBrush.Realb.opacity * 100,
        state->currentBrush.Realb.crv * 100,
        BParam_GetValue(&bpSpacing), BParam_GetValue(&bpScatter));
    ImVec2 tsz = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(gcx - tsz.x / 2, gcy + gizR + 8), IM_COL32(211, 211, 211, 230), buf);

    // ── Color swatch ────────────────────────────────────────────────────
    int swY = gcy + gizR + 14;
    Color curCol = HSLToRGB(colorHue, colorSat, colorLit);
    dl->AddRectFilled(ImVec2(gcx - 50, swY), ImVec2(gcx + 50, swY + 24), IM_COL32(curCol.r, curCol.g, curCol.b, 255));
    dl->AddRect(ImVec2(gcx - 50, swY), ImVec2(gcx + 50, swY + 24), IM_COL32_WHITE);

    // ── Radial input handling ───────────────────────────────────────────
    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - gcx, dy = mp.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);

    bool radialHovered = (dist < gizR && dist > 3);
    bool down = ImGui::IsMouseDown(0);
    bool clicked = ImGui::IsMouseClicked(0);
    bool released = ImGui::IsMouseReleased(0);

    if (clicked && !ImGui::IsAnyItemHovered() && radialHovered) {
        float angDeg = (ang + (float)M_PI) * 180.0f / (float)M_PI;
        float rotDiff = fabsf(angDeg - fmodf(state->currentBrush.Realb.resangle, 360.0f));
        if (rotDiff > 180.0f) rotDiff = 360.0f - rotDiff;
        if (rotDiff < 5.0f) gizmoMouseMode = 4;
        else if (ang > d30 && ang < d30 * 5) gizmoMouseMode = 1;
        else if (ang < d30 && ang > -(float)M_PI * 0.5f) gizmoMouseMode = 2;
        else gizmoMouseMode = 3;
    }

    if (gizmoMouseMode > 0 && down) {
        float rad = dist / state->camera.zoom;
        float absrad = dist;
        int curMode = 0;
        if (ang > d30 && ang < d30 * 5) curMode = 1;
        else if (ang < d30 && ang > -(float)M_PI * 0.5f) curMode = 2;
        else curMode = 3;

        if (gizmoMouseMode == 1) {
            float rel = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 1;
            if (curMode != 1) rad = roundf(rad / 10.0f) * 10.0f;
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
            float t = ir / fmaxf(1.0f, state->currentBrush.Realb.rad_out);
            state->currentBrush.Realb.crv = 1.0f - t;
            BParam_SetValue(&bpCurvature, 1.0f - t);
        }
        if (gizmoMouseMode == 4) {
            float newAng = (ang + (float)M_PI) * 180.0f / (float)M_PI;
            if (absrad > 160.0f || rad < 20.0f)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->currentBrush.Realb.resangle = newAng;
        }
    }

    if (released) gizmoMouseMode = 0;

    // ── File buttons (top-left) ─────────────────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(vp.x + 8, vp.y + 8));
    for (int i = 0; i < FILE_BTN_N; i++) {
        ImGui::PushID(100 + i);
        ImTextureID tid = (fileBtnTex[i].id > 0) ? (ImTextureID)(intptr_t)fileBtnTex[i].id : 0;
        if (tid) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.27f, 0.31f, 0.86f));
            if (ImGui::ImageButton("##fb", tid, ImVec2(24, 24))) {
                if (i == 0) App_FileNew();
                else if (i == 1) App_FileOpen();
                else if (i == 2) App_FileSaveAs();
                else if (i == 3) App_FileSave();
                else if (i == 5) App_FileSnap();
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.27f, 0.31f, 0.86f));
            if (ImGui::Button(fileBtnLabels[i], ImVec2(36, 28))) {
                if (i == 0) App_FileNew();
                else if (i == 1) App_FileOpen();
                else if (i == 2) App_FileSaveAs();
                else if (i == 3) App_FileSave();
                else if (i == 5) App_FileSnap();
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        ImGui::SameLine();
    }

    // ── Tool buttons (top-right) ────────────────────────────────────────
    int totalToolW = GIZMO_TOOL_N * 40 + (GIZMO_TOOL_N - 1) * 4;
    ImGui::SetCursorScreenPos(ImVec2(vp.x + vp.width - totalToolW - 8, vp.y + 8));
    for (int i = 0; i < GIZMO_TOOL_N; i++) {
        bool active;
        if (i == 3) active = (state->mode == eBrush && state->currentBrush.Realb.col.a == 0);
        else active = (state->mode == gizmoToolModes[i]);

        ImGui::PushID(200 + i);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.43f, 0.75f, 0.78f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.50f, 0.80f, 0.85f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.27f, 0.31f, 0.86f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.40f, 0.90f));
        }

        if (ImGui::Button(gizmoToolLabels[i], ImVec2(40, 28))) {
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
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::SameLine();
    }

    // ── Slider columns ──────────────────────────────────────────────────
    int gap2 = 2;
    int totalColH = GIZMO_PENBTN_H + gap2 + GIZMO_SLIDER_H + gap2 + GIZMO_ICON_H;
    int sliderLeftX = gcx - gizR - 3 * GIZMO_SLIDER_W - 2 * GIZMO_SLIDER_GAP - 12;
    int sliderRightX = gcx + gizR + 12;
    int penBtnY = gcy - totalColH / 2;
    int slY = penBtnY + GIZMO_PENBTN_H + gap2;
    int iconY = slY + GIZMO_SLIDER_H + gap2;

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};
    int colorModes[6] = {-1, -1, -1, 0, 1, 2};

    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP)
            : sliderRightX + (i - 3) * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP);
        BParam* bp = bps[i];

        dl->AddText(ImVec2(colX + GIZMO_SLIDER_W / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

        // Pen mode button
        ImGui::SetCursorScreenPos(ImVec2(colX, penBtnY + 1));
        Texture2D pt = GetPenModeIcon(bp->penMode);
        ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;

        ImGui::PushID(300 + i);
        char pname[32];
        snprintf(pname, sizeof(pname), "penpop_%d", i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        if (penTid) {
            if (ImGui::ImageButton("##pb", penTid, ImVec2(GIZMO_SLIDER_W, GIZMO_PENBTN_H - 2)))
                ImGui::OpenPopup(pname);
        } else {
            if (ImGui::Button("...", ImVec2(GIZMO_SLIDER_W, GIZMO_PENBTN_H - 2)))
                ImGui::OpenPopup(pname);
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopup(pname, ImGuiWindowFlags_NoScrollbar)) {
            for (int p = 0; p < PEN_MODE_COUNT; p++) {
                Texture2D itex = GetPenModeIcon(p);
                if (itex.id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)itex.id, ImVec2(16, 16));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(PenModeNames[p], bp->penMode == p))
                    bp->penMode = p;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        // Slider body
        DrawSliderVertical(dl, bp, colX, slY, GIZMO_SLIDER_W, GIZMO_SLIDER_H, bp->slider.clipmaxF, colorModes[i]);

        // Invisible button for slider drag
        ImGui::PushID(400 + i);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
        ImGui::InvisibleButton("##sb", ImVec2(GIZMO_SLIDER_W, GIZMO_SLIDER_H));
        if (ImGui::IsItemActive()) {
            float t = (mp.y - slY) / (float)GIZMO_SLIDER_H;
            t = fminf(1.0f, fmaxf(0.0f, t));
            t = 1.0f - t;
            bps[i]->slider.clipmaxF = t;
        }
        ImGui::PopID();

        // Icon at bottom
        if (bp->iconLoaded) {
            ImTextureID iconTid = (ImTextureID)(intptr_t)bp->iconTex.id;
            if (iconTid) {
                ImGui::SetCursorScreenPos(ImVec2(colX + (GIZMO_SLIDER_W - 24) / 2, iconY));
                ImGui::Image(iconTid, ImVec2(24, 24));
            }
        } else {
            Color swatch = (i == 3) ? HSLToRGB(colorHue, 1.0f, 0.5f)
                        : (i == 4) ? HSLToRGB(colorHue, colorSat, colorLit)
                        : HSLToRGB(colorHue, colorSat, colorLit);
            ImU32 swCol = IM_COL32(swatch.r, swatch.g, swatch.b, 255);
            dl->AddRectFilled(ImVec2(colX + 4, iconY + 2), ImVec2(colX + GIZMO_SLIDER_W - 4, iconY + GIZMO_ICON_H - 2), swCol);
            dl->AddRect(ImVec2(colX + 4, iconY + 2), ImVec2(colX + GIZMO_SLIDER_W - 4, iconY + GIZMO_ICON_H - 2), IM_COL32(200, 200, 200, 200));
        }
    }

    ImGui::End();
}

void Gizmo_DrawPenPopups(AppState*) {}
