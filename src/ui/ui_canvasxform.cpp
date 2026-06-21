#include "repaint.h"
#include "layerstack.h"
#include "transform_handle.h"
#include "imgui.h"
#include <math.h>
#include <string.h>

extern bool layersDirty;

// ── Drag state ───────────────────────────────────────────────────────
static RectXform g_entryWindow = {};  // saved on entering crop mode (for discard)
static bool     g_entrySaved = false;
static Vector2  g_cropCursor = {0, 0}; // independent rotation center
static float    g_ppiSlider = 0.0f;

static void UpdateCanvasPipeline(AppState* state, const RectXform* rx) {
    int outW = DocOutPxW(&state->doc), outH = DocOutPxH(&state->doc);
    float cv[6]; ComputeCanvasMatrix(state->doc.ppu, rx, outW, outH, cv);
    LayerStack_SetCanvasView(cv);
    LayerStack_SetRenderWindow(outW, outH);
    layersDirty = true;
}

static void ExitCropMode(AppState* state, bool accept) {
    if (accept) {
        ApplyCanvasWindow(&state->doc);
    } else {
        state->doc.window = g_entryWindow;
        int cw = DocOutPxW(&state->doc), ch = DocOutPxH(&state->doc);
        float cv[6]; ComputeCanvasMatrix(state->doc.ppu, &state->doc.window, cw, ch, cv);
        LayerStack_SetCanvasView(cv); LayerStack_SetRenderWindow(cw, ch);
    }
    state->framingMode = FRAME_DEFAULT;
    g_activeHud = HUD_NONE;
    state->camera.target = Vector2{0, 0};
    state->camera.zoom = 1.0f;
    g_entrySaved = false;
    layersDirty = true;
}

void CanvasXformModule::OnExit() {
    if (state->framingMode == FRAME_CROP)
        ExitCropMode(state, true);
}

bool CanvasXformModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (g_activeHud != HUD_CANVAS_XFORM) {
        g_entrySaved = false;
        return false;
    }

    // Save initial canvas window on first frame after entering crop mode
    if (!g_entrySaved) {
        g_entryWindow = state->doc.window;
        g_entrySaved = true;
        // Init crop cursor to window center
        float* m = state->doc.window.mat;
        float w = state->doc.window.w, h = state->doc.window.h;
        g_cropCursor.x = m[2] + (m[0]*w + m[1]*h) * 0.5f;
        g_cropCursor.y = m[5] + (m[3]*w + m[4]*h) * 0.5f;
    }

    // Accept / discard keys
    if (IsKeyPressed(KEY_E)) {
        ExitCropMode(state, true);
        return true;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        ExitCropMode(state, false);
        return true;
    }

    if (ImGui::IsAnyItemHovered()) {
        input.mouseCaptured = true;
        return true;
    }

    // Pan and zoom pass through
    if (IsKeyDown(KEY_SPACE) || GetMouseWheelMove() != 0.0f) return false;

    if (!rect.Contains(input.MousePos())) return false;

    bool changed = TransformHandle_Input(&state->doc.window, &g_cropCursor,
        false,  // scaleProportionalToCursor=false → corner-extent (crop)
        &state->camera, input.MousePos(),
        input.MouseDown(MOUSE_LEFT_BUTTON),
        input.MousePressed(MOUSE_LEFT_BUTTON),
        input.MouseDown(MOUSE_RIGHT_BUTTON),
        input.MousePressed(MOUSE_RIGHT_BUTTON),
        &rect);

    if (changed) {
        UpdateCanvasPipeline(state, &state->doc.window);
    }

    return changed;
}

void CanvasXformModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_CANVAS_XFORM) return;

    TransformHandle_Draw(&state->doc.window, g_cropCursor, &state->camera);
}

void CanvasXformModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_CANVAS_XFORM) return;

    float bx = rect.x + 6;
    float by = rect.y + 6;
    float bw = 140;
    ImGui::SetNextWindowPos(ImVec2(bx, by), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bw, 0), ImGuiCond_Always);
    ImGui::Begin("##canvasOps", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Canvas Window");
    ImGui::Separator();
    int curW = DocOutPxW(&state->doc), curH = DocOutPxH(&state->doc);
    ImGui::Text("ppu: %.2f", state->doc.ppu);
    ImGui::Text("Size: %.0f x %.0f", state->doc.window.w, state->doc.window.h);
    ImGui::Text("rot: %.1f", RectXform_GetRot(&state->doc.window) * 180.0f / (float)M_PI);

    ImGui::Separator();
    ImGui::Text("Output: %d x %d", curW, curH);
    ImGui::Separator();
    ImGui::Text("PPI");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ppi", &g_ppiSlider, -1.0f, 1.0f, "%.2f");
    bool ppiEdited = ImGui::IsItemDeactivatedAfterEdit();
    if (g_ppiSlider != 0.0f) {
        float previewPPU = state->doc.ppu * powf(2.0f, g_ppiSlider);
        ImGui::Separator();
        ImGui::SetWindowFontScale(1.5f);
        ImGui::Text("%.0f x %.0f", previewPPU * state->doc.window.w,
                    previewPPU * state->doc.window.h);
        ImGui::SetWindowFontScale(1.0f);
    }
    if (ppiEdited) {
        state->doc.ppu *= powf(2.0f, g_ppiSlider);
        g_ppiSlider = 0.0f;
        int outW = DocOutPxW(&state->doc), outH = DocOutPxH(&state->doc);
        float cv[6]; ComputeCanvasMatrix(state->doc.ppu, &state->doc.window, outW, outH, cv);
        LayerStack_SetCanvasView(cv);
        LayerStack_SetRenderWindow(outW, outH);
        layersDirty = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Accept (E)", ImVec2(-1, 0))) {
        ExitCropMode(state, true);
    }
    if (ImGui::Button("Discard (ESC)", ImVec2(-1, 0))) {
        ExitCropMode(state, false);
    }

    ImGui::End();
}
