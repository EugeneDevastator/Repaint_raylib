#include "repaint.h"
#include "layerstack.h"
#include "transform_handle.h"
#include "imgui.h"

extern bool layersDirty;

bool LayerXformModule::HandleInput(InputState& input, const DrawRect& rect) {
    // Toggle mode on KEY_ONE — block while editing ImGui text widgets
    if (!ImGui::IsAnyItemActive() && input.KeyPressed(KEY_ONE)) {
        if (g_activeHud == HUD_LAYER_XFORM) {
            HudSetActive(state, HUD_NONE);
        } else {
            HudSetActive(state, HUD_LAYER_XFORM);
            DisplayInfoText("Transform");
            if (state->activeLayer >= 0) {
                sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
                float* m = lp->xform.mat;
                g_pivotCursorX = m[2] + m[0]*lp->layerW*0.5f + m[1]*lp->layerH*0.5f;
                g_pivotCursorY = m[5] + m[3]*lp->layerW*0.5f + m[4]*lp->layerH*0.5f;
            }
        }
        return true;
    }

    if (g_activeHud != HUD_LAYER_XFORM) return false;

    // Capture clicks on imgui buttons
    if (ImGui::IsAnyItemHovered()) {
        input.mouseCaptured = true;
        return true;
    }

    // Pan and zoom pass through
    if (IsKeyDown(KEY_SPACE) || GetMouseWheelMove() != 0.0f) return false;

    // Out of bounds or no layer
    if (!rect.Contains(input.MousePos()) || state->activeLayer < 0 ||
        state->activeLayer >= LayerStack_Count()) {
        return false;
    }

    sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
    // Sync xform extent from pixel dimensions (world units = pixels at ppu=1)
    lp->xform.w = (float)lp->layerW;
    lp->xform.h = (float)lp->layerH;
    if (lp->xform.w < 1.0f) lp->xform.w = state->doc.window.w;
    if (lp->xform.h < 1.0f) lp->xform.h = state->doc.window.h;

    Vector2 cursor = {g_pivotCursorX, g_pivotCursorY};
    bool changed = TransformHandle_Input(&lp->xform, &cursor,
        true,  // scaleProportionalToCursor=true → cursor-centered (layer)
        &state->camera, input.MousePos(),
        input.MouseDown(MOUSE_LEFT_BUTTON),
        input.MousePressed(MOUSE_LEFT_BUTTON),
        input.MouseDown(MOUSE_RIGHT_BUTTON),
        input.MousePressed(MOUSE_RIGHT_BUTTON),
        &rect);

    g_pivotCursorX = cursor.x;
    g_pivotCursorY = cursor.y;

    if (changed) layersDirty = true;

    // Always consume while active
    return true;
}

void LayerXformModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_LAYER_XFORM || state->activeLayer < 0) return;
    if (state->activeLayer >= LayerStack_Count()) return;

    sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
    TransformHandle_Draw(&lp->xform,
        Vector2{g_pivotCursorX, g_pivotCursorY},
        &state->camera);
}

void LayerXformModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_LAYER_XFORM || state->activeLayer < 0) return;

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
