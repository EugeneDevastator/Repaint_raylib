#ifndef UI_PAINTHUD_H
#define UI_PAINTHUD_H

// ── Painting mode HUD ────────────────────────────────────────────────
// Shows an XOR circle with brush radius and XOR crosshair at the viewport
// center when the painting mode is active.
// Activated by HUD_PAINTING (5).

class PaintHudModule : public IModule {
public:
    PaintHudModule(AppState* s) : state(s) {}
    void DrawGL(const DrawRect& rect) override;
    bool HandleInput(InputState& input, const DrawRect& rect) override;
private:
    AppState* state;
};

#endif
