#include "repaint.h"
#include "brush_preset.h"
#include "rlgl.h"
#include "imgui.h"
#include <math.h>

void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Draw(AppState* state, Rectangle vp);

int quickPanelMouseMode = 0;
bool g_colorPicking = false;
Color g_colorPickGrid[25] = {};
int g_colorPickScreenX = 0, g_colorPickScreenY = 0;
Rectangle g_colorPickVpBounds = {0, 0, 0, 0};

void QuickPanel_Init(void) {
    FilePanel_Init();
    ToolBox_Init();
}

void QuickPanel_Shutdown(void) {
    FilePanel_Shutdown();
    ToolBox_Shutdown();
}

void QuickPanel_DrawUI(AppState* state) {
    if (g_activeHud != HUD_QUICK) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int gizR = 200;
    float dThick = fmaxf(20.0f, fminf(sw / 12.0f, 48.0f));
    float dLen   = fmaxf(100.0f, fminf(sh / 3.0f, 400.0f));
    int dCtrl    = (int)dThick;
    int dGap     = fmaxf(4, dCtrl / 3);
    int dSpacing = fmaxf(2, dCtrl / 6);

    int totalColH = dCtrl + dSpacing + (int)dLen + dSpacing + dCtrl;
    int sliderLeftX = gcx - gizR - 3 * dCtrl - 2 * dGap - 12;
    int sliderRightX = gcx + gizR + 12;
    int penBtnY = gcy - totalColH / 2;
    int slY = penBtnY + dCtrl + dSpacing;
    int iconY = slY + (int)dLen + dSpacing;

    // ── Horizontal brush sliders (between viewport left and left slider column) ──
    {
        int panelW = (int)((sliderLeftX - (int)vp.x - dGap * 2) * 1.5f);
        if (panelW > 270) panelW = 270;
        if (panelW < 80)  panelW = 80;
        int panelX = sliderLeftX - dGap - panelW;
        ImGui::SetCursorScreenPos(ImVec2(panelX, penBtnY - 14));
        ImGui::BeginChild("##sliders", ImVec2((float)panelW, (float)totalColH), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 5));
        BParam* hbps[] = {&bpSize, &bpSizeMul, &bpHardness, &bpCurvature, &bpAngle, &bpScaleRel, &bpCloneOpacity, &bpPerspective, &bpFocalOffset};
        for (int i = 0; i < 9; i++)
            DrawSlider(hbps[i], 0);
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};


    rlSetBlendMode(RL_BLEND_ALPHA);
    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (dCtrl + dGap)
            : sliderRightX + (i - 3) * (dCtrl + dGap);
        BParam* bp = bps[i];

        dl->AddText(ImVec2(colX + dCtrl / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

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
        if (penTid)
            ImGui::ImageButton("##pb", penTid, ImVec2(dCtrl, dCtrl));
        else
            ImGui::Button("...", ImVec2(dCtrl, dCtrl));
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        static int vActivePenId = -1;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(0))
            vActivePenId = i;
        if (vActivePenId == i) {
            ImVec2 btnMin = ImGui::GetItemRectMin();
            float ow = 170.0f;
            ImGui::SetNextWindowPos(ImVec2(btnMin.x, btnMin.y + dCtrl + 2));
            ImGui::SetNextWindowSize(ImVec2(ow, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
            ImGui::Begin("##vPenOverlay", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings);
            bool mouseDown = ImGui::IsMouseDown(0);
            for (int p = 0; p < PEN_MODE_COUNT; p++) {
                ImGui::PushID(p);
                if (penModeTex[p].id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)penModeTex[p].id, ImVec2(16, 16));
                    ImGui::SameLine();
                }
                ImGui::Selectable(PenModeNames[p], bp->penMode == p);
                if (mouseDown && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                    bp->penMode = p;
                ImGui::PopID();
            }
            ImGui::End();
            ImGui::PopStyleColor();
            if (!ImGui::IsMouseDown(0))
                vActivePenId = -1;
        }
        ImGui::PopID();

        rlSetBlendMode(RL_BLEND_ALPHA);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
        DrawSlider(bp, 1, (float)dCtrl, (float)dLen);

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

    // ── Toggle button for texture panel (bottom-aligned with color sliders) ──
    static bool s_showTex = true;
    int texBtnX = sliderRightX + 3 * (dCtrl + dGap);
    ImGui::SetCursorScreenPos(ImVec2((float)texBtnX, (float)iconY));
    if (ImGui::Button(s_showTex ? "Brush Textures" : "Brush Textures", ImVec2((float)(dCtrl * 2 + dGap *2), (float)dCtrl))) {
        s_showTex = !s_showTex;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(s_showTex ? "Hide Texture Panel" : "Show Texture Panel");

    // TexPanelModule draws the texture panel after this, inside ##qpui
    g_texPanelAreaY = s_showTex ? (iconY + dCtrl + 14) : 0;
}
