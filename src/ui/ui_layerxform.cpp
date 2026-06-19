#include "repaint.h"
#include "layerstack.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"
#include <math.h>
#include <string.h>

extern bool layersDirty;

// ── Drag state ───────────────────────────────────────────────────────
static int   g_dragAction = 0;  // 1=drag layer, 2=drag pivot, 3=rotate, 4=scale
static Vector2 g_dragStart = {0, 0};
static float g_savedMat[6];
static int   g_dragCorner = -1;

bool LayerXformModule::HandleInput(InputState& input, const DrawRect& rect) {
    // Toggle mode on KEY_ONE
    if (input.KeyPressed(KEY_ONE)) {
        if (g_activeHud == HUD_LAYER_XFORM) {
            g_activeHud = HUD_NONE;
            g_dragAction = 0;
            g_dragCorner = -1;
        } else {
            g_activeHud = HUD_LAYER_XFORM;
            if (state->activeLayer >= 0) {
                g_pivotCursorX = state->doc.window.cx;
                g_pivotCursorY = state->doc.window.cy;
            }
        }
        return true;
    }

    if (g_activeHud != HUD_LAYER_XFORM) {
        g_dragAction = 0;
        g_dragCorner = -1;
        memset(g_savedMat, 0, sizeof(g_savedMat));
        return false;
    }

    // Capture clicks on our imgui buttons (not background — corner handles
    // are raylib draws outside imgui, so IsAnyItemHovered is safe here)
    if (ImGui::IsAnyItemHovered()) {
        input.mouseCaptured = true;
        return true;
    }

    // Pan and zoom pass through to viewport
    bool spaceHeld = IsKeyDown(KEY_SPACE);
    float scroll = GetMouseWheelMove();
    if (spaceHeld || scroll != 0.0f) return false;

    // Out of bounds or no layer — let viewport handle zoom/pan but not painting
    if (!rect.Contains(input.MousePos()) || state->activeLayer < 0 ||
        state->activeLayer >= LayerStack_Count()) {
        return false;
    }

    sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
    float lw = (float)lp->layerW, lh = (float)lp->layerH;
    if (lw < 1) lw = (float)DocOutW(&state->doc);
    if (lh < 1) lh = (float)DocOutH(&state->doc);

    Vector2 mousePos = input.MousePos();
    Vector2 canvasPos = GetScreenToWorld2D(mousePos, state->camera);

    // Compute 4 corners in canvas + screen space
    float a = lp->mat[0], b = lp->mat[1], tx = lp->mat[2];
    float c = lp->mat[3], d = lp->mat[4], ty = lp->mat[5];
    Vector2 corners[4] = {
        {0*a+0*b+tx, 0*c+0*d+ty},
        {lw*a+0*b+tx, lw*c+0*d+ty},
        {lw*a+lh*b+tx, lw*c+lh*d+ty},
        {0*a+lh*b+tx, 0*c+lh*d+ty}
    };
    Vector2 sc[4];
    for (int ci = 0; ci < 4; ci++)
        sc[ci] = GetWorldToScreen2D(corners[ci], state->camera);

    float cDist = Dist2D(mousePos, GetWorldToScreen2D(
        Vector2{g_pivotCursorX, g_pivotCursorY}, state->camera));
    bool nearCenter = cDist < 12.0f;

    int nearCorner = -1;
    for (int ci = 0; ci < 4; ci++) {
        if (Dist2D(mousePos, sc[ci]) < 12.0f) { nearCorner = ci; break; }
    }

    if (input.MousePressed(MOUSE_LEFT_BUTTON)) {
        if (nearCenter) {
            g_dragAction = 2;
        } else if (nearCorner >= 0) {
            g_dragAction = 4;
            g_dragCorner = nearCorner;
        } else {
            g_dragAction = 1;
        }
        g_dragStart = canvasPos;
        memcpy(g_savedMat, lp->mat, sizeof(g_savedMat));
    }

    // Drag layer
    if (g_dragAction == 1 && input.MouseDown(MOUSE_LEFT_BUTTON)) {
        float mdx = canvasPos.x - g_dragStart.x;
        float mdy = canvasPos.y - g_dragStart.y;
        memcpy(lp->mat, g_savedMat, sizeof(g_savedMat));
        float tmat[6] = {1, 0, mdx, 0, 1, mdy};
        LayerStack_ApplyTransform(state->activeLayer, tmat);
        layersDirty = true;
    }

    // Drag pivot
    if (g_dragAction == 2 && input.MouseDown(MOUSE_LEFT_BUTTON)) {
        g_pivotCursorX = canvasPos.x;
        g_pivotCursorY = canvasPos.y;
    }

    // Start rotate
    if (input.MousePressed(MOUSE_RIGHT_BUTTON)) {
        g_dragAction = 3;
        g_dragStart = canvasPos;
        memcpy(g_savedMat, lp->mat, sizeof(g_savedMat));
    }

    // Rotate
    if (g_dragAction == 3 && input.MouseDown(MOUSE_RIGHT_BUTTON)) {
        float startAng = atan2f(g_dragStart.y - g_pivotCursorY,
                                g_dragStart.x - g_pivotCursorX);
        float curAng  = atan2f(canvasPos.y - g_pivotCursorY,
                               canvasPos.x - g_pivotCursorX);
        float deltaDeg = (curAng - startAng) * (180.0f / (float)M_PI);
        if (deltaDeg > 180.0f) deltaDeg -= 360.0f;
        else if (deltaDeg < -180.0f) deltaDeg += 360.0f;
        float cosD = cosf(deltaDeg * (float)M_PI / 180.0f);
        float sinD = sinf(deltaDeg * (float)M_PI / 180.0f);
        float pivX = g_pivotCursorX, pivY = g_pivotCursorY;
        float mat[6] = {
            cosD, -sinD, pivX - pivX * cosD + pivY * sinD,
            sinD,  cosD, pivY - pivX * sinD - pivY * cosD
        };
        memcpy(lp->mat, g_savedMat, sizeof(g_savedMat));
        LayerStack_ApplyTransform(state->activeLayer, mat);
        layersDirty = true;
    }

    // Scale
    if (g_dragAction == 4 && input.MouseDown(MOUSE_LEFT_BUTTON)) {
        memcpy(lp->mat, g_savedMat, sizeof(g_savedMat));
        float as = g_savedMat[0], bs = g_savedMat[1], ts = g_savedMat[2];
        float cs = g_savedMat[3], ds = g_savedMat[4], tys = g_savedMat[5];

        float fixX = g_pivotCursorX, fixY = g_pivotCursorY;
        float dx = canvasPos.x - fixX;
        float dy = canvasPos.y - fixY;

        float det = as * ds - bs * cs;
        if (fabsf(det) > 0.0001f) {
            float invDet = 1.0f / det;
            float ia = ds * invDet, ib = -bs * invDet;
            float ic = -cs * invDet, id = as * invDet;

            float pcx = (fixX - ts) * ia + (fixY - tys) * ib;
            float pcy = (fixX - ts) * ic + (fixY - tys) * id;

            float lx = dx * ia + dy * ib;
            float ly = dx * ic + dy * id;

            int gc = g_dragCorner;
            float grabLx = (gc == 0 || gc == 3) ? 0.0f : lw;
            float grabLy = (gc == 0 || gc == 1) ? 0.0f : lh;

            float initDx = grabLx - pcx;
            float initDy = grabLy - pcy;

            float sx = (fabsf(initDx) > 0.001f) ? lx / initDx : 1.0f;
            float sy = (fabsf(initDy) > 0.001f) ? ly / initDy : 1.0f;
            if (fabsf(sx) < 0.01f) sx = (sx < 0) ? -0.01f : 0.01f;
            if (fabsf(sy) < 0.01f) sy = (sy < 0) ? -0.01f : 0.01f;

            float oldSx = sqrtf(as * as + cs * cs);
            float oldSy = sqrtf(bs * bs + ds * ds);
            float cosR = (oldSx > 0.0001f) ? as / oldSx : 1.0f;
            float sinR = (oldSx > 0.0001f) ? cs / oldSx : 0.0f;

            float newSx = oldSx * sx;
            float newSy = oldSy * sy;
            float m0 = cosR * newSx, m1 = -sinR * newSy;
            float m3 = sinR * newSx, m4 =  cosR * newSy;

            float m2  = fixX - (m0 * pcx + m1 * pcy);
            float m5  = fixY - (m3 * pcx + m4 * pcy);

            lp->mat[0] = m0; lp->mat[1] = m1; lp->mat[2] = m2;
            lp->mat[3] = m3; lp->mat[4] = m4; lp->mat[5] = m5;
        }
        layersDirty = true;
    }

    // Release
    if (input.MouseReleased(MOUSE_LEFT_BUTTON)) {
        if (g_dragAction == 1) {
            g_pivotCursorX = canvasPos.x;
            g_pivotCursorY = canvasPos.y;
        }
        g_dragAction = 0;
        g_dragCorner = -1;
    }
    if (input.MouseReleased(MOUSE_RIGHT_BUTTON))
        g_dragAction = 0;

    // Only consume when actively dragging — let zoom/pan through otherwise
    return (g_dragAction != 0);
}

void LayerXformModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_LAYER_XFORM || state->activeLayer < 0) return;
    if (state->activeLayer >= LayerStack_Count()) return;

    sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
    float lw = (float)lp->layerW, lh = (float)lp->layerH;
    if (lw < 1) lw = (float)DocOutW(&state->doc);
    if (lh < 1) lh = (float)DocOutH(&state->doc);

    auto ws = [&](Vector2 wp) -> Vector2 {
        return GetWorldToScreen2D(wp, state->camera);
    };

    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    Vector2 uip = ws(Vector2{g_pivotCursorX, g_pivotCursorY});
    float chLen = 12.0f;
    DrawLine(uip.x - chLen, uip.y, uip.x + chLen, uip.y, WHITE);
    DrawLine(uip.x, uip.y - chLen, uip.x, uip.y + chLen, WHITE);
    DrawCircle(uip.x, uip.y, 3.0f, WHITE);

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
