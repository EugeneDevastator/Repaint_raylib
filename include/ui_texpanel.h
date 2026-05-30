#pragma once
#include "ui_rect.h"

struct AppState;

struct TexPanelModule : IModule {
    AppState* state;
    TexPanelModule(AppState* s) : state(s) {}
    const char* Name() const override { return "TexPanel"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};
