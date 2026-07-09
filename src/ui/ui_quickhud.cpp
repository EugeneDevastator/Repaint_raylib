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

    // ── Debug overlay (red squares = screen corners, green circles = xform backprojection) ──
    if (g_activeHud == HUD_QUICK) {
        // Static screen corners — independent of zoom/pan, purely fixed pixel positions
        float sx = vp.x + 100.0f, sy = vp.y + 100.0f;
        float sz = 200.0f;  // 200×200 pixel region
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        // Screen corners — red filled squares
        dl->AddRectFilled(ImVec2(sx-3, sy-3), ImVec2(sx+3, sy+3), IM_COL32(255,0,0,255));
        dl->AddRectFilled(ImVec2(sx+sz-3, sy-3), ImVec2(sx+sz+3, sy+3), IM_COL32(255,0,0,255));
        dl->AddRectFilled(ImVec2(sx+sz-3, sy+sz-3), ImVec2(sx+sz+3, sy+sz+3), IM_COL32(255,0,0,255));
        dl->AddRectFilled(ImVec2(sx-3, sy+sz-3), ImVec2(sx+3, sy+sz+3), IM_COL32(255,0,0,255));
        // World corners → screen (green circles)
        Vector2 wTL = GetScreenToWorld2D({sx, sy}, state->camera);
        Vector2 wBR = GetScreenToWorld2D({sx+sz, sy+sz}, state->camera);
        float wcx = fminf(wTL.x,wBR.x), wcy = fminf(wTL.y,wBR.y);
        float ww = fabsf(wBR.x-wTL.x), wh = fabsf(wBR.y-wTL.y);
        Vector2 wc[4] = {{wcx,wcy},{wcx+ww,wcy},{wcx+ww,wcy+wh},{wcx,wcy+wh}};
        for(int i=0;i<4;i++){
            Vector2 s = GetWorldToScreen2D(wc[i], state->camera);
            dl->AddCircleFilled(ImVec2(s.x,s.y), 5, IM_COL32(0,255,0,255));
        }
    }
}
