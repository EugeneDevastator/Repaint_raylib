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
static int      g_cropPixelW = 0;      // resolution text fields (pixels)
static int      g_cropPixelH = 0;

static void ExitCropMode(AppState* state, bool accept) {
    if (accept) {
        ApplyCanvasWindow(&state->doc);
        state->camera.target = Vector2{
            state->doc.window.mat[0]*state->doc.window.ww*0.5f + state->doc.window.mat[1]*state->doc.window.wh*0.5f + state->doc.window.mat[2],
            state->doc.window.mat[3]*state->doc.window.ww*0.5f + state->doc.window.mat[4]*state->doc.window.wh*0.5f + state->doc.window.mat[5]
        };
    } else {
        state->doc.window = g_entryWindow;
        int cw = DocOutPxW(&state->doc), ch = DocOutPxH(&state->doc);
        float cv[6]; ComputeCanvasMatrix(&state->doc.window, cw, ch, cv);
        LayerStack_SetCanvasView(cv); LayerStack_SetRenderWindow(cw, ch);
    }
    state->framingMode = FRAME_DEFAULT;
    g_activeHud = HUD_NONE;
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
        float w = state->doc.window.ww, h = state->doc.window.wh;
        g_cropCursor.x = m[2] + (m[0]*w + m[1]*h) * 0.5f;
        g_cropCursor.y = m[5] + (m[3]*w + m[4]*h) * 0.5f;
    }

    // Block transform interaction while editing ImGui text fields
    if (ImGui::IsAnyItemActive()) {
        input.mouseCaptured = ImGui::IsAnyItemHovered();
        return false;
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
        false,  // lockAspect=false
        &state->camera, input.MousePos(),
        input.MouseDown(MOUSE_LEFT_BUTTON),
        input.MousePressed(MOUSE_LEFT_BUTTON),
        input.MouseDown(MOUSE_RIGHT_BUTTON),
        input.MousePressed(MOUSE_RIGHT_BUTTON),
        &rect);

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

    // Pixel-resolution text fields — sync from doc when not actively editing
    if (!ImGui::IsAnyItemActive()) {
        g_cropPixelW = DocOutPxW(&state->doc);
        g_cropPixelH = DocOutPxH(&state->doc);
    }
    ImGui::Text("Res (px)");
    ImGui::SetNextItemWidth(70);
    bool wEdited = ImGui::InputInt("##cw", &g_cropPixelW, 0, 0);
    bool wDeact = ImGui::IsItemDeactivatedAfterEdit();
    if (wEdited && g_cropPixelW < 1) g_cropPixelW = 1;
    ImGui::SameLine();
    ImGui::Text("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    bool hEdited = ImGui::InputInt("##ch", &g_cropPixelH, 0, 0);
    bool hDeact = ImGui::IsItemDeactivatedAfterEdit();
    if (hEdited && g_cropPixelH < 1) g_cropPixelH = 1;
    if (wDeact || hDeact) {
        float aspect = g_entryWindow.ww / g_entryWindow.wh;
        if (wDeact) {
            state->doc.window.ww = (float)g_cropPixelW;
            state->doc.window.wh = state->doc.window.ww / aspect;
            g_cropPixelH = (int)(state->doc.window.wh + 0.5f);
        } else {
            state->doc.window.wh = (float)g_cropPixelH;
            state->doc.window.ww = state->doc.window.wh * aspect;
            g_cropPixelW = (int)(state->doc.window.ww + 0.5f);
        }
    }

    ImGui::Text("rot: %.1f", RectXform_GetRot(&state->doc.window) * 180.0f / (float)M_PI);
    ImGui::Separator();
    if (ImGui::Button("Accept (E)", ImVec2(-1, 0))) {
        ExitCropMode(state, true);
    }
    if (ImGui::Button("Discard (ESC)", ImVec2(-1, 0))) {
        ExitCropMode(state, false);
    }

    ImGui::End();
}
