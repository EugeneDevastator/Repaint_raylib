#include "ui_leftpanel.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdint>

extern bool layersDirty;
extern bool panelResizing;
extern int uiPanelWidth;
extern BParam bpOpacity, bpSize, bpHardness, bpSpacing, bpCurvature, bpScatter, bpCloneOpacity, bpSizeMul, bpPower, bpPerspective;

Texture2D g_blendModeIcon = {0};
bool g_blendIconLoaded = false;

void LeftPanel_Init(void) {
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
    if (g_blendIconLoaded) {
        UnloadTexture(g_blendModeIcon);
        g_blendIconLoaded = false;
    }
}

void LeftPanel_Draw(AppState* state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)uiPanelWidth, (float)GetScreenHeight()), ImGuiCond_Always);
    ImGui::Begin("Tools", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::Separator();
    ImGui::Text("Settings");
    ImGui::Spacing();

    // BParam sliders
    BParam* bps[] = {&bpSize, &bpSizeMul, &bpHardness, &bpCurvature, &bpSpacing, &bpOpacity, &bpAngle, &bpScaleRel, &bpCloneOpacity, &bpScatter, &bpPower, &bpPerspective};
    for (int i = 0; i < 12; i++)
        DrawSlider(bps[i], 0);

    ImGui::Spacing();

    // Blend mode
    {
        int blend = (int)state->currentBrush.Realb.bmidx;
        if (blend < 0 || blend >= g_blendModeCount) blend = 0;
        DrawRadioGroup("Blend Mode", &blend, g_blendModeNames, g_blendModeCount);
        state->currentBrush.Realb.bmidx = (uint8_t)blend;
    }

    ImGui::Spacing();
    ImGui::Checkbox("Seamless", &g_seamlessPaint);
    ImGui::Checkbox("Seamless Preview", &g_seamlessPreview);

    ImGui::Spacing();
    int preserve = state->currentBrush.Realb.preserveop;
    ImGui::Checkbox("Preserve Layer Alpha", (bool*)&preserve);
    state->currentBrush.Realb.preserveop = (uint8_t)preserve;

    extern bool g_pixelPerfect;
    ImGui::Checkbox("Pixel Perfect", &g_pixelPerfect);

    extern int g_strokeSmoothingMode;
    extern float g_strokeThrottle;
    ImGui::Text("Smoothing");
    ImGui::RadioButton("Linear", &g_strokeSmoothingMode, SMOOTH_MODE_LINEAR); ImGui::SameLine();
    ImGui::RadioButton("Smooth", &g_strokeSmoothingMode, SMOOTH_MODE_SMOOTH);

    if (g_strokeSmoothingMode == SMOOTH_MODE_SMOOTH) {
        ImGui::Indent(10);
        ImGui::SetNextItemWidth(-15);
        ImGui::SliderFloat("Throttle", &g_strokeThrottle, 0.0f, 100.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higher values = fewer segment endpoints = smoother curved strokes");
        ImGui::Unindent(10);
    }

    ImGui::Separator();
    ImGui::Text("Debug");
    ImGui::Spacing();

    // ── Test broker ──
    extern bool g_useTestBroker;
    ImGui::Checkbox("Test Broker (+200px X)", &g_useTestBroker);

    // Zoom and mode info
    {
        char zoomInfo[32];
        sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
        ImGui::Text("%s", zoomInfo);

        const char* modeNames[] = {"Brush", "Smudge", "PolyStripe", "Distort", "Contrast", "Single"};
        ImGui::Text("%s", modeNames[state->mode > 5 ? 0 : state->mode]);
    }

    if (ImGui::Button("Reload Shaders", ImVec2(-1, 0))) {
        BrushBlend_Shutdown();
        BrushBlend_Init();
        ReloadViewportShader();
    }

    if (ImGui::Button("Changelog", ImVec2(-1, 0))) {
        Changelog_Toggle();
    }

    // Separator + resize handle at right edge (drawn inside ImGui for proper z-order)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        float handleX = wMin.x + wSize.x;
        ImU32 col = panelResizing ? IM_COL32(80, 120, 200, 255) : IM_COL32(160, 160, 160, 255);
        float sh = (float)GetScreenHeight();
        dl->AddRectFilled(ImVec2(handleX - 3, wMin.y), ImVec2(handleX + 4, wMin.y + sh), col);

        // Invisible button for resize interaction
        ImGui::SetCursorScreenPos(ImVec2(handleX - 3, wMin.y));
        ImGui::InvisibleButton("##resize", ImVec2(7, sh));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            panelResizing = true;
            float mx = ImGui::GetMousePos().x;
            uiPanelWidth = (int)fmaxf(120.0f, fminf(mx, (float)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH - 100)));
            Rectangle vb = {(float)uiPanelWidth, 0,
                (float)(GetScreenWidth() - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)GetScreenHeight()};
            Viewport_SetBounds(&viewport, vb);
        } else {
            panelResizing = false;
        }
    }

    ImGui::End();
}

// ── LeftPanelModule ───────────────────────────────────────────────────────

bool LeftPanelModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (input.mouseCaptured) return false;
    if (!rect.Contains(input.MousePos())) return false;
    input.mouseCaptured = true;
    return true;
}

void LeftPanelModule::DrawGUI(const DrawRect& rect) {
    if (rect.w < 1 || rect.h < 1) return;
    LeftPanel_Draw(state);
}
