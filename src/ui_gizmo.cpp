#include "repaint.h"
#include "imgui.h"
#include <math.h>

// Gizmo layout constants
#define GIZMO_SLIDER_W 24
#define GIZMO_SLIDER_H 256
#define GIZMO_SLIDER_GAP 8
#define GIZMO_CTRL_SZ 24
#define GIZMO_SPACING 4

// Sub-component declarations (defined in their own files)
void BrushPreview_RenderStamp(AppState*, float drawRadOut);
void BrushPreview_DrawStamp(ImDrawList* dl, ImVec2 org, float drawRadOut);
void DrawSliderVertical(ImDrawList*, BParam*, int x, int y, int w, int h, float val, int colorMode);
void BrushGizmo_Draw(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState*);
void QuickInfo_Draw(ImDrawList*, int gcx, int gcy, int gizR, AppState*);
void FilePanel_Draw(AppState*, Rectangle vp);
void ToolBox_Draw(AppState*, Rectangle vp);

bool gizmoShow = false;
int gizmoMouseMode = 0;
bool g_colorPicking = false;

void Gizmo_HandleInput(AppState*, Vector2) {}

void LoadGizmoIcons(void) {
    BrushPreview_Init();
    FilePanel_Init();
    ToolBox_Init();
}

void UnloadGizmoIcons(void) {
    BrushPreview_Shutdown();
    FilePanel_Shutdown();
    ToolBox_Shutdown();
}

void Gizmo_Draw(AppState* state) {
    if (!gizmoShow) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    float drawRadOut = state->currentBrush.Realb.rad_out * state->camera.zoom;
    float drawRadIn = state->currentBrush.Realb.rad_in * state->camera.zoom;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;

    // ── Render stamp texture before ImGui window (avoids state conflicts) ─
    BrushPreview_RenderStamp(state, drawRadOut);

    // ── Transparent overlay for gizmo visuals + interactive controls ─────
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
    int gizR = 200;
    float d30 = (float)(M_PI * 30.0 / 180.0);

    BrushPreview_DrawStamp(dl, org, drawRadOut);
    BrushGizmo_Draw(dl, org, gcx, gcy, state);
    QuickInfo_Draw(dl, gcx, gcy, gizR, state);

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

    // ── Slider columns ──────────────────────────────────────────────────
    int totalColH = GIZMO_CTRL_SZ + GIZMO_SPACING + GIZMO_SLIDER_H + GIZMO_SPACING + GIZMO_CTRL_SZ;
    int sliderLeftX = gcx - gizR - 3 * GIZMO_SLIDER_W - 2 * GIZMO_SLIDER_GAP - 12;
    int sliderRightX = gcx + gizR + 12;
    int penBtnY = gcy - totalColH / 2;
    int slY = penBtnY + GIZMO_CTRL_SZ + GIZMO_SPACING;
    int iconY = slY + GIZMO_SLIDER_H + GIZMO_SPACING;

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};
    int colorModes[6] = {-1, -1, -1, 0, 1, 2};

    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP)
            : sliderRightX + (i - 3) * (GIZMO_SLIDER_W + GIZMO_SLIDER_GAP);
        BParam* bp = bps[i];

        // Label
        dl->AddText(ImVec2(colX + GIZMO_SLIDER_W / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

        // Pen mode button — GIZMO_CTRL_SZ square
        ImGui::SetCursorScreenPos(ImVec2(colX, penBtnY));
        Texture2D pt = GetPenModeIcon(bp->penMode);
        ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;

        ImGui::PushID(300 + i);
        char pname[32];
        snprintf(pname, sizeof(pname), "penpop_%d", i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        if (penTid) {
            if (ImGui::ImageButton("##pb", penTid, ImVec2(GIZMO_CTRL_SZ, GIZMO_CTRL_SZ)))
                ImGui::OpenPopup(pname);
        } else {
            if (ImGui::Button("...", ImVec2(GIZMO_CTRL_SZ, GIZMO_CTRL_SZ)))
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

        // Invisible button — direct position mapping
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

        // Icon at bottom — GIZMO_CTRL_SZ square
        if (bp->iconLoaded) {
            ImTextureID iconTid = (ImTextureID)(intptr_t)bp->iconTex.id;
            if (iconTid) {
                ImGui::SetCursorScreenPos(ImVec2(colX, iconY));
                ImGui::Image(iconTid, ImVec2(GIZMO_CTRL_SZ, GIZMO_CTRL_SZ));
            }
        } else {
            Color swatch = (i == 3) ? HSLToRGB(colorHue, 1.0f, 0.5f)
                        : (i == 4) ? HSLToRGB(colorHue, colorSat, colorLit)
                        : HSLToRGB(colorHue, colorSat, colorLit);
            ImU32 swCol = IM_COL32(swatch.r, swatch.g, swatch.b, 255);
            dl->AddRectFilled(ImVec2(colX, iconY), ImVec2(colX + GIZMO_CTRL_SZ, iconY + GIZMO_CTRL_SZ), swCol);
            dl->AddRect(ImVec2(colX, iconY), ImVec2(colX + GIZMO_CTRL_SZ, iconY + GIZMO_CTRL_SZ), IM_COL32(200, 200, 200, 200));
        }
    }

    ImGui::End();

    // ── Separate panels (outside transparent overlay) ───────────────────
    if (gizmoShow) {
        FilePanel_Draw(state, vp);
        ToolBox_Draw(state, vp);
    }
}

void Gizmo_DrawPenPopups(AppState*) {}
