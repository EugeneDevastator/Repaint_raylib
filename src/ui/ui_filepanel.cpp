#include "repaint.h"
#include "imgui.h"
#include <stdio.h>

#define FILE_BTN_N 8
static const char* fileBtnLabels[FILE_BTN_N] = {"New","Open","SaveAs","Save","Reload","Snap","Export","Pin"};
static Texture2D fileBtnTex[FILE_BTN_N];

void FilePanel_Init(void) {
    const char* names[FILE_BTN_N] = {"btnnew","btnopen","btnsaveas","btnsave","btnreload","btnsnap","btnexport","btnpin"};
    for (int i = 0; i < FILE_BTN_N; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", names[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 36, 36);
            fileBtnTex[i] = LoadTextureFromImage(img);
            UnloadImage(img);
        } else {
            fileBtnTex[i] = Texture2D{0};
        }
    }
}

void FilePanel_Shutdown(void) {
    for (int i = 0; i < FILE_BTN_N; i++) {
        if (fileBtnTex[i].id > 0) UnloadTexture(fileBtnTex[i]);
    }
}

void FilePanel_Draw(AppState* state, Rectangle vp) {
    ImGui::SetNextWindowPos(ImVec2(vp.x + 8, vp.y + 8));
    ImGui::SetNextWindowSize(ImVec2(FILE_BTN_N * 48 + (FILE_BTN_N - 1) * 4 + 16, 52));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##filepanel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::SetCursorPos(ImVec2(4, 6));
    for (int i = 0; i < FILE_BTN_N; i++) {
        ImGui::PushID(100 + i);

        ImTextureID tid = (fileBtnTex[i].id > 0) ? (ImTextureID)(intptr_t)fileBtnTex[i].id : 0;
        if (tid) {
            if (ImGui::ImageButton("##fb", tid, ImVec2(36, 36))) {
                if (i == 0) App_FileNew();
                else if (i == 1) App_FileOpen();
                else if (i == 2) App_FileSaveAs();
                else if (i == 3) App_FileSave();
                else if (i == 4) App_FileReload();
                else if (i == 5) App_FileSnap();
                else if (i == 6) App_FileExportPNG();
            }
        } else {
            if (ImGui::Button(fileBtnLabels[i], ImVec2(50, 40))) {
                if (i == 0) App_FileNew();
                else if (i == 1) App_FileOpen();
                else if (i == 2) App_FileSaveAs();
                else if (i == 3) App_FileSave();
                else if (i == 4) App_FileReload();
                else if (i == 5) App_FileSnap();
                else if (i == 6) App_FileExportPNG();
            }
        }

        ImGui::PopID();
        if (i < FILE_BTN_N - 1) ImGui::SameLine();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
