#include "repaint.h"

void QuickPanel_DrawUI(AppState* state);
void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);

bool QuickHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_QUICK) return false;
    input.mouseCaptured = true;
    return true;
}

void QuickHudModule::DrawGL(const DrawRect& rect) {
    (void)rect;
    XORgizmo_DrawVisual(state);
}

void QuickHudModule::DrawGUI(const DrawRect& rect) {
    (void)rect;
    if (g_activeHud != HUD_QUICK) return;
    QuickPanel_DrawUI(state);
    XORgizmo_HandleInput(state);
}
