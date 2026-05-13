#include "repaint.h"
#include "imgui.h"
#include <math.h>

// Sub-component declarations (defined in their own files)
void BrushGizmo_Draw(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState*);
void QuickInfo_Draw(ImDrawList*, int gcx, int gcy, int gizR, AppState*);
void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Draw(AppState* state, Rectangle vp);

bool quickPanelShow = false;
int quickPanelMouseMode = 0;
bool g_colorPicking = false;
Color g_colorPickGrid[9] = {};

void QuickPanel_Init(void) {
    BrushPreview_Init();
    FilePanel_Init();
    ToolBox_Init();
}

void QuickPanel_Shutdown(void) {
    BrushPreview_Shutdown();
    FilePanel_Shutdown();
    ToolBox_Shutdown();
}

void QuickPanel_Draw(AppState* state) {
    if (!quickPanelShow) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    // Use base slider values (not velocity-modulated) for gizmo display
    float baseRadOut = BParam_GetValue(&bpSize);
    float baseHard   = BParam_GetValue(&bpHardness);
    float drawRadOut = baseRadOut * state->camera.zoom;
    float drawRadIn  = baseRadOut * baseHard * state->camera.zoom;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;

    // ── Separate panels (top-level windows, drawn before overlay) ────────
    FilePanel_Draw(state, vp);
    ToolBox_Draw(state, vp);

    // ── Transparent overlay for gizmo visuals + interactive controls ─────
    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, vp.height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##quickpanel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 org((float)gcx, (float)gcy);
    int gizR = 200;
    float d30 = (float)(M_PI * 30.0 / 180.0);

    BrushGizmo_Draw(dl, org, gcx, gcy, state);
    QuickInfo_Draw(dl, gcx, gcy, gizR, state);

    // ── Radial input handling (sector-based, no distance limit) ─────────
    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - gcx, dy = mp.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);

    bool down = ImGui::IsMouseDown(0);
    bool clicked = ImGui::IsMouseClicked(0);
    bool released = ImGui::IsMouseReleased(0);

    if (clicked && !ImGui::IsAnyItemHovered()) {
        // Sector-based mode detection
        bool inSector1 = (ang > d30 && ang < d30 * 5);       // size sector
        bool inSector2 = (ang < d30 && ang > -(float)M_PI * 0.5f);  // hardness sector
        bool inSector3 = !inSector1 && !inSector2;            // curve sector

        if (dist <= GIZMO_FIXED_RADIUS_PX) {
            // Inside fixed radius: hardness, curve, or size
            if (inSector1) quickPanelMouseMode = 1;
            else if (inSector2) quickPanelMouseMode = 2;
            else quickPanelMouseMode = 3;
        } else {
            // Outside fixed radius: size in sector 1, otherwise rotation
            quickPanelMouseMode = inSector1 ? 1 : 4;
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
            if (state->currentBrush.Realb.rad_in > newRad * 0.98f)
                state->currentBrush.Realb.rad_in = newRad * 0.98f;
            BParam_SetValue(&bpSize, newRad);
            float h = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 2) {
            // Hardness: captured inside radius, but tracks continuously once active
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 2) h = 0.0f;
            if (h < 0.05f) h = 0.0f;
            state->currentBrush.Realb.rad_in = h * state->currentBrush.Realb.rad_out;
            if (state->currentBrush.Realb.rad_in > state->currentBrush.Realb.rad_out * 0.98f)
                state->currentBrush.Realb.rad_in = state->currentBrush.Realb.rad_out * 0.98f;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 3) {
            // Curve: captured inside radius, but tracks continuously once active
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 3) h = 0.0f;
            state->currentBrush.Realb.crv = 1.0f - h;
            BParam_SetValue(&bpCurvature, 1.0f - h);
        }
        if (quickPanelMouseMode == 4) {
            float newAng = (ang + (float)M_PI) * 180.0f / (float)M_PI;
            if (absrad > 160.0f || rad < 20.0f)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->currentBrush.Realb.resangle = newAng;
        }
    }

    if (released) quickPanelMouseMode = 0;

    // ── Slider columns (dynamic sizes proportional to viewport) ────────
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float dThick = fmaxf(20.0f, fminf(sw / 12.0f, 48.0f));   // 1/12 screen width
    float dLen   = fmaxf(100.0f, fminf(sh / 3.0f, 400.0f));  // 1/3 screen height
    int dCtrl    = (int)dThick;
    int dGap     = fmaxf(4, dCtrl / 3);
    int dSpacing = fmaxf(2, dCtrl / 6);

    int totalColH = dCtrl + dSpacing + (int)dLen + dSpacing + dCtrl;
    int sliderLeftX = gcx - gizR - 3 * dCtrl - 2 * dGap - 12;
    int sliderRightX = gcx + gizR + 12;
    int penBtnY = gcy - totalColH / 2;
    int slY = penBtnY + dCtrl + dSpacing;
    int iconY = slY + (int)dLen + dSpacing;

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};
    int colorModes[6] = {-1, -1, -1, 0, 1, 2};

    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (dCtrl + dGap)
            : sliderRightX + (i - 3) * (dCtrl + dGap);
        BParam* bp = bps[i];

        dl->AddText(ImVec2(colX + dCtrl / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

        // Pen mode button
        ImGui::SetCursorScreenPos(ImVec2(colX, penBtnY));
        Texture2D pt = GetPenModeIcon(bp->penMode);
        ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;

        ImGui::PushID(300 + i);
        char pname[32];
        snprintf(pname, sizeof(pname), "penpop_%d", i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (penTid) {
            if (ImGui::ImageButton("##pb", penTid, ImVec2(dCtrl, dCtrl)))
                ImGui::OpenPopup(pname);
        } else {
            if (ImGui::Button("...", ImVec2(dCtrl, dCtrl)))
                ImGui::OpenPopup(pname);
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

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
        DrawSliderVertical(dl, bp, colX, slY, dCtrl, (int)dLen, bp->slider.clipmaxF, colorModes[i]);

        // Invisible button — per-item activation via ImGui button flags
        ImGui::PushID(400 + i);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
        ImGui::InvisibleButton("##sb", ImVec2(dCtrl, (int)dLen),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        if (ImGui::IsItemActive()) {
            float t = (mp.y - slY) / (float)dLen;
            t = fminf(1.0f, fmaxf(0.0f, t));
            t = 1.0f - t;
            if (ImGui::IsMouseDown(0))
                bps[i]->slider.clipmaxF = t;
            else if (ImGui::IsMouseDown(1))
                bps[i]->slider.clipminF = t;
            else if (ImGui::IsMouseDown(2))
                bps[i]->slider.jitter = t;
        }
        ImGui::PopID();

        // Icon at bottom
        if (bp->iconLoaded) {
            ImTextureID iconTid = (ImTextureID)(intptr_t)bp->iconTex.id;
            if (iconTid) {
                ImGui::SetCursorScreenPos(ImVec2(colX, iconY));
                ImGui::Image(iconTid, ImVec2(dCtrl, dCtrl));
            }
        } else {
            Color swatch = (i == 3) ? HSLToRGB(colorHue, 1.0f, 0.5f)
                        : (i == 4) ? HSLToRGB(colorHue, colorSat, colorLit)
                        : HSLToRGB(colorHue, colorSat, colorLit);
            ImU32 swCol = IM_COL32(swatch.r, swatch.g, swatch.b, 255);
            dl->AddRectFilled(ImVec2(colX, iconY), ImVec2(colX + dCtrl, iconY + dCtrl), swCol);
            dl->AddRect(ImVec2(colX, iconY), ImVec2(colX + dCtrl, iconY + dCtrl), IM_COL32(200, 200, 200, 200));
        }
    }

    ImGui::End();
}
