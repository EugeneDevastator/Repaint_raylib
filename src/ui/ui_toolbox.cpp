#include "repaint.h"
#include "imgui.h"

#define GIZMO_TOOL_N 6
static const char* gizmoToolLabels[GIZMO_TOOL_N] = {"Br","Sm","Po","Er","Di","Co"};
static const int gizmoToolModes[GIZMO_TOOL_N] = {eBrush, eSmudge, ePolyStripe, eBrush, eDistort, eContrast};

#define TOOL_ICON_N 6
static Texture2D toolIconTex[TOOL_ICON_N];
static const char* toolIconNames[TOOL_ICON_N] = {"tbrush","tsmudge","tline","tdisp","tcont","tcont"};

void ToolBox_Init(void) {
    for (int i = 0; i < TOOL_ICON_N; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", toolIconNames[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 36, 36);
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
    static const int toolIconIdx[GIZMO_TOOL_N] = {0, 1, 2, 4, 3, 4};
    int cols = 3, rows = 2;
    int btnSz = 42;
    int gap = 4;
    int winW = cols * btnSz + (cols - 1) * gap + 12;
    int winH = rows * btnSz + (rows - 1) * gap + 12;

    ImGui::SetNextWindowPos(ImVec2(vp.x + vp.width / 2 - winW / 2, vp.y + 8));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##toolbox", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

    for (int i = 0; i < GIZMO_TOOL_N; i++) {
        int col = i % cols, row = i / cols;
        ImGui::SetCursorPos(ImVec2(4 + col * (btnSz + gap), 4 + row * (btnSz + gap)));
        ImGui::PushID(200 + i);

        int ii = toolIconIdx[i];
        bool hasIcon = (ii >= 0 && ii < TOOL_ICON_N && toolIconTex[ii].id > 0);
        ImTextureID tid = hasIcon ? (ImTextureID)(intptr_t)toolIconTex[ii].id : 0;

        auto handleClick = [&]() {
            if (i == 0) DisplayInfoText("Painting");
            if (i == 3) {
                state->mode = eBrush;
                if (state->eraseMode == eEraseNone) {
                    state->eraseMode = eEraseAlpha;
                } else {
                    ImGui::OpenPopup("##eraseModePopup");
                }
            } else {
                state->mode = gizmoToolModes[i];
                state->eraseMode = eEraseNone;
                state->currentBrush.Realb.col.a = 255;
            }
        };

        bool isActive = false;
        if (i == 3)
            isActive = (state->eraseMode != eEraseNone);
        else
            isActive = (state->mode == gizmoToolModes[i]);

        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.9f, 1.0f, 1.0f));
        }
        if (tid) {
            if (ImGui::ImageButton("##ti", tid, ImVec2(btnSz, btnSz)))
                handleClick();
        } else {
            if (ImGui::Button(gizmoToolLabels[i], ImVec2(btnSz, btnSz)))
                handleClick();
        }
        if (isActive) ImGui::PopStyleColor(isActive ? 2 : 0);

        ImGui::PopID();
    }

    if (ImGui::BeginPopup("##eraseModePopup", ImGuiWindowFlags_NoScrollbar)) {
        static const char* eraseNames[] = {"Alpha Erase", "Color Erase"};
        static const int eraseModes[] = {eEraseAlpha, eEraseColor};
        for (int i = 0; i < 2; i++) {
            if (ImGui::Selectable(eraseNames[i], state->eraseMode == eraseModes[i])) {
                state->eraseMode = eraseModes[i];
                state->mode = eBrush;
            }
        }
        if (ImGui::Selectable("Disable Erase", state->eraseMode == eEraseNone)) {
            state->eraseMode = eEraseNone;
            state->mode = eBrush;
            state->currentBrush.Realb.col.a = 255;
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
