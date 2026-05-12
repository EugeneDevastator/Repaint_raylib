#include "ui_leftpanel.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdint>

extern bool layersDirty;
extern bool panelResizing;
extern int uiPanelWidth;
extern BParam bpOpacity, bpSize, bpHardness, bpSpacing, bpCurvature, bpScatter;

static RenderTexture2D stampPrev = {0};
static bool stampPrevInited = false;
Texture2D g_blendModeIcon = {0};
bool g_blendIconLoaded = false;

void LeftPanel_Init(void) {
    if (!stampPrevInited) {
        stampPrev = LoadRenderTexture(100, 100);
        stampPrevInited = true;
    }
    if (!g_blendIconLoaded) {
        if (FileExists("resources/ctlbm.png")) {
            Image img = LoadImage("resources/ctlbm.png");
            ImageResize(&img, 24, 24);
            g_blendModeIcon = LoadTextureFromImage(img);
            g_blendIconLoaded = g_blendModeIcon.id > 0;
            UnloadImage(img);
        }
    }
}

void LeftPanel_Shutdown(void) {
    if (stampPrevInited && stampPrev.id > 0) {
        UnloadRenderTexture(stampPrev);
        stampPrevInited = false;
    }
    if (g_blendIconLoaded) {
        UnloadTexture(g_blendModeIcon);
        g_blendIconLoaded = false;
    }
}

void LeftPanel_Draw(AppState* state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)uiPanelWidth, (float)SCREEN_HEIGHT), ImGuiCond_Always);
    ImGui::Begin("Tools", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Brush type
    {
        static const char* blendNames[] = {
            "Normal","Add","Dodge","Screen","Lighten","Burn",
            "Multiply","Darken","Overlay","Highlight","Shadowlight",
            "Xor","Diff","Exclusion"
        };
        int blend = (int)state->currentBrush.Realb.bmidx;
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##brushBlend", &blend, blendNames, 14, 14))
            state->currentBrush.Realb.bmidx = (uint8_t)blend;
    }

    ImGui::Separator();
    ImGui::Text("Settings");
    ImGui::Spacing();

    // Brush stamp preview
    if (stampPrevInited && stampPrev.id > 0) {
        BeginTextureMode(stampPrev);
        ClearBackground(BLANK);
        EndTextureMode();

        d_Brush pb = state->currentBrush;
        Color pc = pb.Realb.col;
        pb.Realb.col = WHITE;
        pb.Realb.opacity = 1.0f;
        float prevRadOut = pb.Realb.rad_out;
        if (prevRadOut > 45.0f) {
            pb.Realb.rad_out = 45.0f;
            pb.Realb.rad_in = pb.Realb.rad_in * (45.0f / fmaxf(prevRadOut, 1.0f));
        }
        BrushBlend_ApplyStamp(stampPrev, &pb, 50, 50);
        pb.Realb.rad_out = prevRadOut;
        pb.Realb.col = pc;

        float cw = ImGui::GetContentRegionAvail().x;
        float previewSize = fminf(cw, 100.0f);
        ImGui::SetCursorPosX((cw - previewSize) * 0.5f);
        ImGui::Image((ImTextureID)(intptr_t)stampPrev.texture.id,
            ImVec2(previewSize, previewSize));
    }

    // BParam sliders
    BParam* bps[] = {&bpSize, &bpHardness, &bpCurvature, &bpSpacing, &bpOpacity, &bpScatter};
    for (int i = 0; i < 6; i++)
        DrawBParamSlider(bps[i]);

    ImGui::Spacing();
    int preserve = state->currentBrush.Realb.preserveop;
    ImGui::Checkbox("Preserve Layer Alpha", (bool*)&preserve);
    state->currentBrush.Realb.preserveop = (uint8_t)preserve;

    // Pipeline selector
    {
        const char* pipeNames[] = {"CFNSR", "RS"};
        int pipe = (int)state->currentBrush.Realb.pipeID;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##pipeline", &pipe, pipeNames, 2))
            state->currentBrush.Realb.pipeID = (uint8_t)pipe;
    }

    // Reload shaders button at bottom
    {
        float windowH = ImGui::GetWindowHeight();
        float itemH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorPosY(windowH - itemH * 3 - ImGui::GetStyle().FramePadding.y * 2);

        if (ImGui::Button("Reload Shaders", ImVec2(-1, 0))) {
            BrushBlend_Shutdown();
            BrushBlend_Init();
            ReloadViewportShader();
        }
    }

    // Zoom and mode info at bottom
    {
        float windowH = ImGui::GetWindowHeight();
        float itemH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorPosY(windowH - itemH * 2);

        char zoomInfo[32];
        sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
        ImGui::Text("%s", zoomInfo);

        const char* modeNames[] = {"None", "Brush", "Smudge", "Disp", "Cont", "Line"};
        ImGui::Text("%s", modeNames[state->mode > 5 ? 0 : state->mode]);
    }

    // Separator + resize handle at right edge (drawn inside ImGui for proper z-order)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        float handleX = wMin.x + wSize.x;
        ImU32 col = panelResizing ? IM_COL32(80, 120, 200, 255) : IM_COL32(160, 160, 160, 255);
        dl->AddRectFilled(ImVec2(handleX - 3, wMin.y), ImVec2(handleX + 4, wMin.y + SCREEN_HEIGHT), col);

        // Invisible button for resize interaction
        ImGui::SetCursorScreenPos(ImVec2(handleX - 3, wMin.y));
        ImGui::InvisibleButton("##resize", ImVec2(7, SCREEN_HEIGHT));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            panelResizing = true;
            float mx = ImGui::GetMousePos().x;
            uiPanelWidth = (int)fmaxf(120.0f, fminf(mx, (float)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH - 100)));
            Rectangle vb = {(float)uiPanelWidth, 0,
                (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)SCREEN_HEIGHT};
            Viewport_SetBounds(&viewport, vb);
        } else {
            panelResizing = false;
        }
    }

    ImGui::End();
}
