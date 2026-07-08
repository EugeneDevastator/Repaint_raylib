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
static int      g_origTexW = 0;        // texture size on crop entry (for discard restore)
static int      g_origTexH = 0;
static int      g_dragTexW = 0;        // texture size at drag start (for proportional adj)
static int      g_dragTexH = 0;
static float    g_dragWw = 0;          // ww/wh at drag start
static float    g_dragWh = 0;
static bool     g_dragging = false;

static void ExitCropMode(AppState* state, bool accept) {
    if (accept) {
        ApplyCanvasWindow(&state->doc);
        state->camera.target = Vector2{
            state->doc.window.mat[0]*state->doc.window.ww*0.5f + state->doc.window.mat[1]*state->doc.window.wh*0.5f + state->doc.window.mat[2],
            state->doc.window.mat[3]*state->doc.window.ww*0.5f + state->doc.window.mat[4]*state->doc.window.wh*0.5f + state->doc.window.mat[5]
        };
    } else {
        state->doc.window = g_entryWindow;
        LayerStack_ResizeCanvas(g_origTexW, g_origTexH);
        LayerStack_SetCanvasXform(&state->doc.window);
        float cv[6]; ComputeCanvasMatrix(&state->doc.window, g_origTexW, g_origTexH, cv);
        LayerStack_SetCanvasView(cv);
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

    // Save initial canvas window + texture size on first frame after entering crop mode
    if (!g_entrySaved) {
        g_entryWindow = state->doc.window;
        g_origTexW = LayerStack_RenderW();
        g_origTexH = LayerStack_RenderH();
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

    // On first change of a drag: save the starting ww/wh and texture size
    if (changed && !g_dragging) {
        g_dragWw = state->doc.window.ww;
        g_dragWh = state->doc.window.wh;
        g_dragTexW = LayerStack_RenderW();
        g_dragTexH = LayerStack_RenderH();
        g_dragging = true;
    }

    // While dragging: proportionally adjust texture size.
    // tex = savedTex * (savedWw / currentWw), etc.
    if (g_dragging) {
        float curWw = state->doc.window.ww;
        float curWh = state->doc.window.wh;
        if (curWw > 0.5f && curWh > 0.5f) {
            float deltaW = curWw/g_dragWw;
            float deltaH = curWh/g_dragWh;
            int newW = fmaxf(1, (int)(g_dragTexW * deltaW + 0.5f));
            int newH = fmaxf(1, (int)(g_dragTexH * deltaH + 0.5f));
            LayerStack_ResizeCanvas(newW, newH);
            layersDirty = true;
        }
    }

    // On mouse release: finalize drag
    if (!input.MouseDown(MOUSE_LEFT_BUTTON) && !input.MouseDown(MOUSE_RIGHT_BUTTON))
        g_dragging = false;

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
    float bw = 200;
    ImGui::SetNextWindowPos(ImVec2(bx, by), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bw, 0), ImGuiCond_Always);
    ImGui::Begin("##canvasOps", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Canvas Window");
    ImGui::Separator();

    // Pixel-resolution text fields — sync from actual texture size, not ww/wh
    if (!ImGui::IsAnyItemActive()) {
        g_cropPixelW = LayerStack_RenderW();
        g_cropPixelH = LayerStack_RenderH();
    }
    ImGui::Text("Res (px)");
    ImGui::SetNextItemWidth(80);
    bool wEdited = ImGui::InputInt("##cw", &g_cropPixelW, 0, 0);
    if (wEdited && g_cropPixelW < 1) g_cropPixelW = 1;
    if (wEdited) {
        int newW = g_cropPixelW < 1 ? 1 : g_cropPixelW;
        float aspect = state->doc.window.ww / state->doc.window.wh;
        int newH = fmaxf(1, (int)(newW / aspect + 0.5f));
        g_cropPixelH = newH;
        LayerStack_ResizeCanvas(newW, newH);
        g_cropPixelW = LayerStack_RenderW();
        g_cropPixelH = LayerStack_RenderH();
        layersDirty = true;
    }
    ImGui::SameLine();
    ImGui::Text("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    bool hEdited = ImGui::InputInt("##ch", &g_cropPixelH, 0, 0);
    if (hEdited && g_cropPixelH < 1) g_cropPixelH = 1;
    if (hEdited) {
        int newH = g_cropPixelH < 1 ? 1 : g_cropPixelH;
        float aspect = state->doc.window.ww / state->doc.window.wh;
        int newW = fmaxf(1, (int)(newH * aspect + 0.5f));
        g_cropPixelW = newW;
        LayerStack_ResizeCanvas(newW, newH);
        g_cropPixelW = LayerStack_RenderW();
        g_cropPixelH = LayerStack_RenderH();
        layersDirty = true;
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
