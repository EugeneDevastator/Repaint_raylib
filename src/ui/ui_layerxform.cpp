#include "repaint.h"
#include "layerstack.h"
#include "transform_handle.h"
#include "imgui.h"

extern bool layersDirty;
bool g_lockAspect = false;

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
                g_pivotCursorX = m[2] + m[0]*GetLayerWpx(state->activeLayer)*0.5f + m[1]*GetLayerHpx(state->activeLayer)*0.5f;
                g_pivotCursorY = m[5] + m[3]*GetLayerWpx(state->activeLayer)*0.5f + m[4]*GetLayerHpx(state->activeLayer)*0.5f;
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
    // Sync xform extent from pixel dimensions
    lp->xform.ww = (float)GetLayerWpx(state->activeLayer);
    lp->xform.wh = (float)GetLayerHpx(state->activeLayer);
    if (lp->xform.ww < 1.0f) lp->xform.ww = state->doc.window.ww;
    if (lp->xform.wh < 1.0f) lp->xform.wh = state->doc.window.wh;

    Vector2 cursor = {g_pivotCursorX, g_pivotCursorY};
    bool changed = TransformHandle_Input(&lp->xform, &cursor,
        true,  // scaleProportionalToCursor=true → cursor-centered (layer)
        g_lockAspect,
        &state->camera, input.MousePos(),
        input.MouseDown(MOUSE_LEFT_BUTTON),
        input.MousePressed(MOUSE_LEFT_BUTTON),
        input.MouseDown(MOUSE_RIGHT_BUTTON),
        input.MousePressed(MOUSE_RIGHT_BUTTON),
        &rect);

    g_pivotCursorX = cursor.x;
    g_pivotCursorY = cursor.y;

    if (changed) layersDirty = true;

    // Repeat last transform on 'R'
    if (!ImGui::IsAnyItemActive() && input.KeyPressed(KEY_R)) {
        TransformHandle_RepeatLast(&lp->xform, Vector2{g_pivotCursorX, g_pivotCursorY});
        layersDirty = true;
        DisplayInfoText("Repeated transform");
    }

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
    float bw = 180;
    ImGui::SetNextWindowPos(ImVec2(bx, by), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bw, 0), ImGuiCond_Always);
    ImGui::Begin("##layerOps", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    // ── Layer info ──
    if (state->activeLayer < LayerStack_Count()) {
        sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
        ImGui::Text("Tex: %d x %d", GetLayerWpx(state->activeLayer), GetLayerHpx(state->activeLayer));
        ImGui::Text("XWH: %.0f x %.0f", lp->xform.ww, lp->xform.wh);
        ImGui::Separator();
    }

    // ── Stored transform editor ──
    float store[6];
    TransformHandle_GetStore(store);

    // Decompose (translation is user-entered, not from stored matrix)
    static float s_editTx = 0.0f, s_editTy = 0.0f;
    float tx = s_editTx, ty = s_editTy;
    float sx = sqrtf(store[0]*store[0] + store[3]*store[3]);
    float sy = sqrtf(store[1]*store[1] + store[4]*store[4]);
    float rotDeg = atan2f(store[3], store[0]) * (180.0f / (float)M_PI);

    ImGui::Text("Transform");
    ImGui::Separator();

    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputFloat("X", &tx, 0, 0, "%.1f");
    changed |= ImGui::InputFloat("Y", &ty, 0, 0, "%.1f");
    changed |= ImGui::InputFloat("SX", &sx, 0, 0, "%.3f");
    changed |= ImGui::InputFloat("SY", &sy, 0, 0, "%.3f");

    float rotClamped = rotDeg;
    changed |= ImGui::InputFloat("Deg", &rotClamped, 0, 0, "%.1f");

    float ratioN = (fabsf(rotClamped) > 0.01f) ? 360.0f / rotClamped : 0.0f;
    float newN = ratioN;
    if (ImGui::InputFloat("1/N", &newN, 0, 0, "%.2f")) {
        if (fabsf(newN) > 0.01f) {
            rotClamped = 360.0f / newN;
            changed = true;
        }
    }

    if (changed) {
        s_editTx = tx; s_editTy = ty;
        // Rebuild matrix: translate * rotate * scale
        float rotRad = rotClamped * ((float)M_PI / 180.0f);
        float mRot[6], mScl[6], mTmp[6];
        Xform_SetRot(mRot, rotRad);
        Xform_SetScale(mScl, sx, sy);
        Xform_Mul(mTmp, mRot, mScl);
        float mTrs[6];
        Xform_SetTrans(mTrs, tx, ty);
        float result[6];
        Xform_Mul(result, mTrs, mTmp);
        TransformHandle_SetStore(result);
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(-1, 0)) && state->activeLayer >= 0) {
        sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
        TransformHandle_RepeatLast(&lp->xform, Vector2{g_pivotCursorX, g_pivotCursorY});
        layersDirty = true;
    }
    ImGui::Checkbox("Lock Aspect", &g_lockAspect);

    ImGui::End();
}
