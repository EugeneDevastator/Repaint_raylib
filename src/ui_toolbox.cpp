#include "repaint.h"
#include "imgui.h"

#define GIZMO_TOOL_N 6
static const char* gizmoToolLabels[GIZMO_TOOL_N] = {"Br","Sm","Li","Er","Di","Co"};
static const int gizmoToolModes[GIZMO_TOOL_N] = {eBrush, eSmudge, eLine, -1, eDisp, eCont};

#define TOOL_ICON_N 5
static Texture2D toolIconTex[TOOL_ICON_N];
static const char* toolIconNames[TOOL_ICON_N] = {"tbrush","tsmudge","tline","tdisp","tcont"};

void ToolBox_Init(void) {
    for (int i = 0; i < TOOL_ICON_N; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", toolIconNames[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 24, 24);
            toolIconTex[i] = LoadTextureFromImage(img);
            UnloadImage(img);
        } else {
            toolIconTex[i] = Texture2D{0};
        }
    }
}

void ToolBox_Shutdown(void) {
    for (int i = 0; i < TOOL_ICON_N; i++) {
        if (toolIconTex[i].id > 0) UnloadTexture(toolIconTex[i]);
    }
}

void ToolBox_Draw(AppState* state, Rectangle vp) {
    // Icon index per tool: Br->0, Sm->1, Li->2, Er->-1(no icon), Di->3, Co->4
    static const int toolIconIdx[GIZMO_TOOL_N] = {0, 1, 2, -1, 3, 4};
    int totalToolW = GIZMO_TOOL_N * 36 + (GIZMO_TOOL_N - 1) * 4;

    ImGui::SetNextWindowPos(ImVec2(vp.x + vp.width - totalToolW - 8, vp.y + 8));
    ImGui::SetNextWindowSize(ImVec2(totalToolW + 16, 40));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##toolbox", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::SetCursorScreenPos(ImVec2(4, 4));
    for (int i = 0; i < GIZMO_TOOL_N; i++) {
        bool active;
        if (i == 3) active = (state->mode == eBrush && state->currentBrush.Realb.col.a == 0);
        else active = (state->mode == gizmoToolModes[i]);

        ImGui::PushID(200 + i);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.45f, 0.45f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.50f, 0.90f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.27f, 0.27f, 0.31f, 0.86f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.40f, 0.90f));
        }

        int ii = toolIconIdx[i];
        bool hasIcon = (ii >= 0 && ii < TOOL_ICON_N && toolIconTex[ii].id > 0);
        if (hasIcon) {
            ImTextureID tid = (ImTextureID)(intptr_t)toolIconTex[ii].id;
            if (ImGui::ImageButton("##ti", tid, ImVec2(28, 28))) {
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
        } else {
            if (ImGui::Button(gizmoToolLabels[i], ImVec2(36, 28))) {
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
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::SameLine();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
