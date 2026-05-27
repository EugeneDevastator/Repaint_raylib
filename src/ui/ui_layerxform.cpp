#include "repaint.h"
#include "layerstack.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"

void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);

bool LayerXformModule::HandleInput(InputState& input, const DrawRect& rect) {
    (void)rect;
    if (input.KeyPressed(KEY_ONE)) {
        if (g_activeHud == HUD_LAYER_XFORM) {
            g_activeHud = HUD_NONE;
        } else {
            g_activeHud = HUD_LAYER_XFORM;
            if (state->activeLayer >= 0) {
                g_pivotCursorX = state->doc.width * 0.5f;
                g_pivotCursorY = state->doc.height * 0.5f;
            }
        }
    }
    // Don't consume input — let other modules (viewport, panels) process normally
    return false;
}

void LayerXformModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_LAYER_XFORM || state->activeLayer < 0) return;

    sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
    float lw = (float)lp->layerW, lh = (float)lp->layerH;
    if (lw < 1) lw = (float)state->doc.width;
    if (lh < 1) lh = (float)state->doc.height;

    auto ws = [&](Vector2 wp) -> Vector2 {
        return GetWorldToScreen2D(wp, state->camera);
    };

    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    // Pivot cursor
    Vector2 uip = ws(Vector2{g_pivotCursorX, g_pivotCursorY});
    float chLen = 12.0f;
    DrawLine(uip.x - chLen, uip.y, uip.x + chLen, uip.y, WHITE);
    DrawLine(uip.x, uip.y - chLen, uip.x, uip.y + chLen, WHITE);
    DrawCircle(uip.x, uip.y, 3.0f, WHITE);

    // Layer outline + scale handles
    float a = lp->mat[0], b = lp->mat[1], tx = lp->mat[2];
    float c = lp->mat[3], d = lp->mat[4], ty = lp->mat[5];
    Vector2 pts[4] = {{0,0}, {lw,0}, {lw,lh}, {0,lh}};
    Vector2 corners[5];
    for (int ci = 0; ci < 4; ci++) {
        float rx = pts[ci].x * a + pts[ci].y * b + tx;
        float ry = pts[ci].x * c + pts[ci].y * d + ty;
        corners[ci] = ws(Vector2{rx, ry});
    }
    corners[4] = corners[0];
    DrawLineStrip(corners, 5, WHITE);

    for (int ci = 0; ci < 4; ci++) {
        float hx = corners[ci].x, hy = corners[ci].y;
        float hs = 10.0f;
        DrawRectangleLinesEx(Rectangle{hx - hs, hy - hs, hs * 2, hs * 2}, 2.0f, WHITE);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}

void LayerXformModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_LAYER_XFORM || state->activeLayer < 0) return;

    // Layer operation buttons at the left edge of the viewport
    float bx = rect.x + 6;
    float by = rect.y + 6;
    float bw = 120;
    ImGui::SetNextWindowPos(ImVec2(bx, by), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bw, 0), ImGuiCond_Always);
    ImGui::Begin("##layerOps", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button("Add layer", ImVec2(-1, 0))) {}
    if (ImGui::Button("Crop canvas", ImVec2(-1, 0))) {}
    if (ImGui::Button("CropWrap", ImVec2(-1, 0))) {}
    if (ImGui::Button("Drop Union", ImVec2(-1, 0))) {}
    if (ImGui::Button("Apply Union", ImVec2(-1, 0))) {}
    if (ImGui::Button("Set Res", ImVec2(-1, 0))) {}
    if (ImGui::Button("Reset Xform", ImVec2(-1, 0))) {}

    ImGui::End();
}
