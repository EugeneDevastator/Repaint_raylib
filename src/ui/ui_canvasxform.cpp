#include "repaint.h"
#include "layerstack.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"
#include <math.h>
#include <string.h>

extern bool layersDirty;

// ── Drag state ───────────────────────────────────────────────────────
static int      g_dragAction = 0;  // 1=translate, 2=scale, 3=rotate
static Vector2  g_dragStart = {0, 0};
static int      g_dragCorner = -1;
static CanvasWindow g_savedWin = {};
static CanvasWindow g_entryWindow = {};  // saved on entering crop mode (for discard)
static bool     g_entrySaved = false;

// Returns true if canvasPos (document-space) is inside the canvas window rect
static bool PointInCanvasWindow(Vector2 canvasPos, const CanvasWindow* cw) {
    float dx = canvasPos.x - cw->cx;
    float dy = canvasPos.y - cw->cy;
    float c = cosf(-cw->rotation), s = sinf(-cw->rotation);
    float localX = dx * c - dy * s;
    float localY = dx * s + dy * c;
    float hw = cw->w * 0.5f, hh = cw->h * 0.5f;
    return fabsf(localX) <= hw && fabsf(localY) <= hh;
}

static void UpdateCanvasPipeline(AppState* state, CanvasWindow* cw) {
    int outW = DocOutW(&state->doc), outH = DocOutH(&state->doc);
    float cv[6]; ComputeCanvasMatrix(state->doc.ppu, cw, outW, outH, cv);
    LayerStack_SetCanvasView(cv);
    LayerStack_SetRenderWindow(outW, outH);
    layersDirty = true;
}

static void ExitCropMode(AppState* state, bool accept) {
    if (accept) {
        ApplyCanvasWindow(&state->doc);
    } else {
        state->doc.window = g_entryWindow;
    }
    { int cw = DocOutW(&state->doc), ch = DocOutH(&state->doc);
      float cv[6]; ComputeCanvasMatrix(state->doc.ppu, &state->doc.window, cw, ch, cv);
      LayerStack_SetCanvasView(cv); LayerStack_SetRenderWindow(cw, ch); }
    state->framingMode = FRAME_DEFAULT;
    g_activeHud = HUD_NONE;
    state->camera.target = Vector2{state->doc.window.cx, state->doc.window.cy};
    state->camera.zoom = 1.0f;
    g_entrySaved = false;
    layersDirty = true;
}

bool CanvasXformModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (g_activeHud != HUD_CANVAS_XFORM) {
        g_dragAction = 0; g_dragCorner = -1;
        memset(&g_savedWin, 0, sizeof(g_savedWin));
        g_entrySaved = false;
        return false;
    }

    // Save initial canvas window on first frame after entering crop mode
    if (!g_entrySaved) {
        g_entryWindow = state->doc.window;
        g_entrySaved = true;
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

    // Pan (space+drag) and zoom (scroll) pass through to viewport
    bool spaceHeld = IsKeyDown(KEY_SPACE);
    float scroll = GetMouseWheelMove();
    if (spaceHeld || scroll != 0.0f) return false;

    if (!rect.Contains(input.MousePos())) return false;

    Vector2 mousePos = input.MousePos();
    Vector2 canvasPos = GetScreenToWorld2D(mousePos, state->camera);
    CanvasWindow* cw = &state->doc.window;

    float c = cosf(cw->rotation), s = sinf(cw->rotation);
    float hw = cw->w * 0.5f, hh = cw->h * 0.5f;
    Vector2 corners_doc[4] = {
        {cw->cx + (-hw)*c - (-hh)*s, cw->cy + (-hw)*s + (-hh)*c},
        {cw->cx + (+hw)*c - (-hh)*s, cw->cy + (+hw)*s + (-hh)*c},
        {cw->cx + (+hw)*c - (+hh)*s, cw->cy + (+hw)*s + (+hh)*c},
        {cw->cx + (-hw)*c - (+hh)*s, cw->cy + (-hw)*s + (+hh)*c},
    };
    Vector2 sc[4];
    Vector2 centerSc = GetWorldToScreen2D(Vector2{cw->cx, cw->cy}, state->camera);
    for (int i = 0; i < 4; i++)
        sc[i] = GetWorldToScreen2D(corners_doc[i], state->camera);

    bool nearCenter = Dist2D(mousePos, centerSc) < 12.0f;
    bool inBody = PointInCanvasWindow(canvasPos, cw);
    int nearCorner = -1;
    for (int i = 0; i < 4; i++) {
        if (Dist2D(mousePos, sc[i]) < 12.0f) { nearCorner = i; break; }
    }

    if (input.MousePressed(MOUSE_LEFT_BUTTON)) {
        if (nearCorner >= 0) {
            g_dragAction = 2;
            g_dragCorner = nearCorner;
        } else if (inBody) {
            g_dragAction = 1;
        } else {
            // Click outside window body — let viewport handle panning
            return false;
        }
        g_dragStart = canvasPos;
        g_savedWin = *cw;
    }

    // Translate
    if (g_dragAction == 1 && input.MouseDown(MOUSE_LEFT_BUTTON)) {
        float dx = canvasPos.x - g_dragStart.x;
        float dy = canvasPos.y - g_dragStart.y;
        cw->cx = g_savedWin.cx + dx;
        cw->cy = g_savedWin.cy + dy;
        UpdateCanvasPipeline(state, cw);
    }

    // Scale from corner
    if (g_dragAction == 2 && input.MouseDown(MOUSE_LEFT_BUTTON)) {
        float sc = cosf(g_savedWin.rotation), ss = sinf(g_savedWin.rotation);
        float dx = canvasPos.x - g_savedWin.cx;
        float dy = canvasPos.y - g_savedWin.cy;
        float initLx = (g_dragCorner == 0 || g_dragCorner == 3) ? -g_savedWin.w * 0.5f : g_savedWin.w * 0.5f;
        float initLy = (g_dragCorner == 0 || g_dragCorner == 1) ? -g_savedWin.h * 0.5f : g_savedWin.h * 0.5f;
        float curLx =  dx * sc + dy * ss;
        float curLy = -dx * ss + dy * sc;
        float sx = (fabsf(initLx) > 0.001f) ? curLx / initLx : 1.0f;
        float sy = (fabsf(initLy) > 0.001f) ? curLy / initLy : 1.0f;
        if (fabsf(sx) < 0.01f) sx = (sx < 0) ? -0.01f : 0.01f;
        if (fabsf(sy) < 0.01f) sy = (sy < 0) ? -0.01f : 0.01f;
        cw->w = g_savedWin.w * sx;
        cw->h = g_savedWin.h * sy;
        if (cw->w < 1.0f) cw->w = 1.0f;
        if (cw->h < 1.0f) cw->h = 1.0f;
        UpdateCanvasPipeline(state, cw);
    }

    // Start rotate (right-click)
    if (input.MousePressed(MOUSE_RIGHT_BUTTON)) {
        if (!inBody) return false; // only rotate when clicking on window
        g_dragAction = 3;
        g_dragStart = canvasPos;
        g_savedWin = *cw;
    }

    // Rotate
    if (g_dragAction == 3 && input.MouseDown(MOUSE_RIGHT_BUTTON)) {
        float startAng = atan2f(g_dragStart.y - g_savedWin.cy, g_dragStart.x - g_savedWin.cx);
        float curAng   = atan2f(canvasPos.y - g_savedWin.cy, canvasPos.x - g_savedWin.cx);
        float delta = curAng - startAng;
        if (delta > (float)M_PI) delta -= 2.0f*(float)M_PI;
        else if (delta < -(float)M_PI) delta += 2.0f*(float)M_PI;
        cw->rotation = g_savedWin.rotation + delta;
        UpdateCanvasPipeline(state, cw);
    }

    // Release
    if (input.MouseReleased(MOUSE_LEFT_BUTTON)) {
        g_dragAction = 0; g_dragCorner = -1;
    }
    if (input.MouseReleased(MOUSE_RIGHT_BUTTON))
        g_dragAction = 0;

    return (g_dragAction != 0);
}

void CanvasXformModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_CANVAS_XFORM) return;

    const CanvasWindow* cw = &state->doc.window;
    float c = cosf(cw->rotation), s = sinf(cw->rotation);
    float hw = cw->w * 0.5f, hh = cw->h * 0.5f;
    Vector2 corners_doc[4] = {
        {cw->cx + (-hw)*c - (-hh)*s, cw->cy + (-hw)*s + (-hh)*c},
        {cw->cx + (+hw)*c - (-hh)*s, cw->cy + (+hw)*s + (-hh)*c},
        {cw->cx + (+hw)*c - (+hh)*s, cw->cy + (+hw)*s + (+hh)*c},
        {cw->cx + (-hw)*c - (+hh)*s, cw->cy + (-hw)*s + (+hh)*c},
    };

    auto ws = [&](Vector2 wp) -> Vector2 {
        return GetWorldToScreen2D(wp, state->camera);
    };

    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    // Center crosshair (pivot)
    Vector2 uip = ws(Vector2{cw->cx, cw->cy});
    float chLen = 12.0f;
    DrawLine(uip.x - chLen, uip.y, uip.x + chLen, uip.y, WHITE);
    DrawLine(uip.x, uip.y - chLen, uip.x, uip.y + chLen, WHITE);
    DrawCircle(uip.x, uip.y, 3.0f, WHITE);

    // Rectangle outline
    Vector2 scrn[5];
    for (int i = 0; i < 4; i++)
        scrn[i] = ws(corners_doc[i]);
    scrn[4] = scrn[0];
    DrawLineStrip(scrn, 5, WHITE);

    // Corner handles
    for (int i = 0; i < 4; i++) {
        float hx = scrn[i].x, hy = scrn[i].y;
        float hs = 10.0f;
        DrawRectangleLinesEx(Rectangle{hx - hs, hy - hs, hs * 2, hs * 2}, 2.0f, WHITE);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
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
    ImGui::Text("ppu: %.2f", state->doc.ppu);
    ImGui::Text("win: %.0f x %.0f", state->doc.window.w, state->doc.window.h);
    ImGui::Text("rot: %.1f", state->doc.window.rotation * 180.0f / (float)M_PI);

    ImGui::Separator();
    if (ImGui::Button("Accept (E)", ImVec2(-1, 0))) {
        ExitCropMode(state, true);
    }
    if (ImGui::Button("Discard (ESC)", ImVec2(-1, 0))) {
        ExitCropMode(state, false);
    }

    ImGui::End();
}
