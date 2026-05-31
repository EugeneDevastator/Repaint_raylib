#include "repaint.h"
#include "ui_texpanel.h"
#include "imgui.h"
#include "rlgl.h"

void QuickPanel_DrawUI(AppState* state);
void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);
void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Draw(AppState* state, Rectangle vp);

QuickHudModule::QuickHudModule(AppState* s) : state(s) {
    texPanelChild = std::unique_ptr<IModule>(new TexPanelModule(s));
}

bool QuickHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (g_activeHud != HUD_QUICK) return false;
    if (texPanelChild->HandleInput(input, rect)) return true;
    input.mouseCaptured = true;
    return true;
}

void QuickHudModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_QUICK) return;
    XORgizmo_DrawVisual(state);
    // TexPanelModule draws XOR handles over the texture preview in DrawGL
    texPanelChild->DrawGL(rect);
}

void QuickHudModule::DrawGUI(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_QUICK) return;

    Rectangle vp = viewport.bounds;
    rlSetBlendMode(RL_BLEND_ALPHA);
    FilePanel_Draw(state, vp);
    rlSetBlendMode(RL_BLEND_ALPHA);
    ToolBox_Draw(state, vp);

    // ── Open the shared ##qpui window ──────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    rlSetBlendMode(RL_BLEND_ALPHA);
    ImGui::Begin("##qpui", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    // ── Presets and sliders ────────────────────────────────────────
    QuickPanel_DrawUI(state);

    // ── Texture panel (child module, drawn inside ##qpui) ──────────
    if (g_texPanelAreaY > 0) {
        DrawRect childR = rect;
        childR.y = g_texPanelAreaY;
        childR.h = rect.y + rect.h - g_texPanelAreaY;
        texPanelChild->DrawGUI(childR);
    }

    ImGui::End(); // ##qpui

    // ── Gizmo — clipped to not overlap the texture panel ───────────
    XORgizmo_HandleInput(state);
}
