#include "repaint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define UI_PANEL_WIDTH 200
#define TOOLBAR_HEIGHT 50

// UI state
UIButton btnBrush = {{10, 60, 180, 40}, GRAY, LIGHTGRAY, false, false, "Brush", 0};
UIButton btnSmudge = {{10, 110, 180, 40}, GRAY, LIGHTGRAY, false, false, "Smudge", 1};
UIButton btnLine = {{10, 160, 180, 40}, GRAY, LIGHTGRAY, false, false, "Line", 2};
UIButton btnEraser = {{10, 210, 180, 40}, GRAY, LIGHTGRAY, false, false, "Eraser", 3};

UIButton btnAddLayer = {{10, 300, 180, 40}, GRAY, LIGHTGRAY, false, false, "Add Layer", 10};
UIButton btnDelLayer = {{10, 350, 180, 40}, GRAY, LIGHTGRAY, false, false, "Delete Layer", 11};

UISlider sliderOpacity = {{10, 420, 180, 30}, 1.0f, 0.0f, 1.0f, GRAY, LIGHTGRAY, false, "Opacity", 0};
UISlider sliderSize = {{10, 470, 180, 30}, 20.0f, 1.0f, 100.0f, GRAY, LIGHTGRAY, false, "Size", 1};
UISlider sliderHardness = {{10, 520, 180, 30}, 0.5f, 0.0f, 1.0f, GRAY, LIGHTGRAY, false, "Hardness", 2};

// Initialize app state
void App_Init(AppState* state) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint - Raylib Port");
    SetTargetFPS(60);

    // Initialize canvas
    state->canvas = Canvas_Create(800, 600, (Color){0, 0, 0, 0});
    state->activeLayer = 0;
    state->scrollPos = (Vector2){0, 0};
    state->zoomK = 1.0f;
    state->mode = 1; // paint mode
    state->leftMouseDown = false;
    state->rightMouseDown = false;
    state->lastMousePos = (Vector2){0, 0};

    // Initialize brush
    state->currentBrush.Realb.rad_in = 10.0f;
    state->currentBrush.Realb.rad_out = 20.0f;
    state->currentBrush.Realb.opacity = 1.0f;
    state->currentBrush.Realb.resangle = 0.0f;
    state->currentBrush.Realb.crv = 0.0f;
    state->currentBrush.Realb.x2y = 1.0f;
    state->currentBrush.Realb.scale = 1.0f;
    state->currentBrush.Realb.cop = 0.0f;
    state->currentBrush.Realb.pwr = 0.0f;
    state->currentBrush.Realb.sol = 1.0f;
    state->currentBrush.Realb.sol2op = 0.0f;
    state->currentBrush.Realb.seed = 0;
    state->currentBrush.Realb.col = BLACK;

    // Generate initial brush texture
    GenerateBrushTexture(&state->currentBrush, &state->brushTexture);

    // Load shaders
    state->brushShader = LoadShader("shaders/brush_gen.vs", "shaders/brush_gen.fs");

    InitCore();
}

// Update UI buttons
void UpdateUI(AppState* state) {
    Vector2 mousePos = GetMousePosition();
    bool mousePressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

    // Update buttons
    UIButton_Update(&btnBrush, mousePos, mousePressed);
    UIButton_Update(&btnSmudge, mousePos, mousePressed);
    UIButton_Update(&btnLine, mousePos, mousePressed);
    UIButton_Update(&btnEraser, mousePos, mousePressed);
    UIButton_Update(&btnAddLayer, mousePos, mousePressed);
    UIButton_Update(&btnDelLayer, mousePos, mousePressed);

    // Update sliders
    UISlider_Update(&sliderOpacity, mousePos, mousePressed);
    UISlider_Update(&sliderSize, mousePos, mousePressed);
    UISlider_Update(&sliderHardness, mousePos, mousePressed);

    // Handle button clicks
    if (btnBrush.clicked) {
        state->mode = 1;
        btnBrush.clicked = false;
    }
    if (btnSmudge.clicked) {
        state->mode = 2;
        btnSmudge.clicked = false;
    }
    if (btnLine.clicked) {
        state->mode = 3;
        btnLine.clicked = false;
    }
    if (btnEraser.clicked) {
        state->mode = 4;
        btnEraser.clicked = false;
    }
    if (btnAddLayer.clicked) {
        Canvas_AddLayer(&state->canvas);
        btnAddLayer.clicked = false;
    }
    if (btnDelLayer.clicked && state->canvas.layerCount > 1) {
        Canvas_DeleteLayer(&state->canvas, state->canvas.layerCount - 1);
        btnDelLayer.clicked = false;
    }

    // Update brush from sliders
    state->currentBrush.Realb.opacity = sliderOpacity.value;
    state->currentBrush.Realb.rad_out = sliderSize.value;
    state->currentBrush.Realb.rad_in = sliderSize.value * sliderHardness.value;

    // Regenerate brush texture if needed
    static float lastSize = 20.0f;
    static float lastHardness = 0.5f;
    if (fabsf(sliderSize.value - lastSize) > 0.5f || fabsf(sliderHardness.value - lastHardness) > 0.01f) {
        UnloadTexture(state->brushTexture);
        GenerateBrushTexture(&state->currentBrush, &state->brushTexture);
        lastSize = sliderSize.value;
        lastHardness = sliderHardness.value;
    }
}

// Draw UI
void DrawUI(AppState* state) {
    // Draw UI panel background (simple rect)
    DrawRectangle(0, 0, UI_PANEL_WIDTH, SCREEN_HEIGHT, (Color){50, 50, 50, 255});
    DrawRectangle(UI_PANEL_WIDTH, 0, 1, SCREEN_HEIGHT, DARKGRAY);

    // Draw title
    DrawText("RePaint", 10, 10, 24, WHITE);
    DrawText("Tools", 10, 40, 20, LIGHTGRAY);

    // Draw buttons
    UIButton_Draw(&btnBrush);
    UIButton_Draw(&btnSmudge);
    UIButton_Draw(&btnLine);
    UIButton_Draw(&btnEraser);

    DrawText("Layers", 10, 280, 20, LIGHTGRAY);
    UIButton_Draw(&btnAddLayer);
    UIButton_Draw(&btnDelLayer);

    DrawText("Settings", 10, 390, 20, LIGHTGRAY);
    UISlider_Draw(&sliderOpacity);
    UISlider_Draw(&sliderSize);
    UISlider_Draw(&sliderHardness);

    // Draw current layer info
    char layerInfo[64];
    sprintf(layerInfo, "Layer: %d/%d", state->activeLayer + 1, state->canvas.layerCount);
    DrawText(layerInfo, 10, SCREEN_HEIGHT - 60, 16, WHITE);

    // Draw zoom info
    char zoomInfo[32];
    sprintf(zoomInfo, "Zoom: %.0f%%", state->zoomK * 100.0f);
    DrawText(zoomInfo, 10, SCREEN_HEIGHT - 40, 16, WHITE);

    // Draw mode info
    const char* modeNames[] = {"None", "Brush", "Smudge", "Line", "Eraser"};
    DrawText(modeNames[state->mode], 10, SCREEN_HEIGHT - 20, 16, GREEN);
}

// Handle canvas input
void HandleCanvasInput(AppState* state) {
    Vector2 mousePos = GetMousePosition();

    // Check if mouse is in canvas area
    bool inCanvas = mousePos.x > UI_PANEL_WIDTH && mousePos.x < SCREEN_WIDTH &&
                   mousePos.y > 0 && mousePos.y < SCREEN_HEIGHT;

    if (!inCanvas) return;

    // Canvas position calculation
    Vector2 canvasPos = {
        (mousePos.x - UI_PANEL_WIDTH - state->scrollPos.x) / state->zoomK,
        (mousePos.y - state->scrollPos.y) / state->zoomK
    };

    // Handle mouse buttons
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && state->mode == 1) {
        // Draw with brush
        if (state->activeLayer >= 0 && state->activeLayer < state->canvas.layerCount) {
            Image layerImage = LoadImageFromTexture(state->canvas.layers[state->activeLayer]);
            ImageDrawCircleV(&layerImage, canvasPos, (int)state->currentBrush.Realb.rad_out, state->currentBrush.Realb.col);
            UnloadTexture(state->canvas.layers[state->activeLayer]);
            state->canvas.layers[state->activeLayer] = LoadTextureFromImage(layerImage);
            UnloadImage(layerImage);
        }
    }

    // Pan with right mouse button or space
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        if (state->leftMouseDown) {
            Vector2 delta = {
                mousePos.x - state->lastMousePos.x,
                mousePos.y - state->lastMousePos.y
            };
            state->scrollPos.x += delta.x;
            state->scrollPos.y += delta.y;
        }
        state->leftMouseDown = true;
    } else {
        state->leftMouseDown = false;
    }

    // Zoom with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        state->zoomK += wheel * 0.1f;
        state->zoomK = fmaxf(0.1f, fminf(5.0f, state->zoomK));
    }

    state->lastMousePos = mousePos;
}

// Main draw function
void App_Draw(AppState* state) {
    BeginDrawing();
    ClearBackground((Color){30, 30, 30, 255});

    // Draw canvas area background
    DrawRectangle(UI_PANEL_WIDTH, 0, SCREEN_WIDTH - UI_PANEL_WIDTH, SCREEN_HEIGHT, (Color){60, 60, 60, 255});

    // Draw canvas with zoom and scroll
    if (state->canvas.layerCount > 0) {
        int canvasX = UI_PANEL_WIDTH + (int)state->scrollPos.x;
        int canvasY = (int)state->scrollPos.y;

        // Draw layers from bottom to top
        for (int i = 0; i < state->canvas.layerCount; i++) {
            if (state->canvas.layerProps[i].visible) {
                Rectangle src = {0, 0, (float)state->canvas.width, (float)state->canvas.height};
                Rectangle dst = {
                    (float)canvasX,
                    (float)canvasY,
                    state->canvas.width * state->zoomK,
                    state->canvas.height * state->zoomK
                };
                DrawTexturePro(state->canvas.layers[i], src, dst, (Vector2){0, 0}, 0.0f, WHITE);
            }
        }
    }

    // Draw UI
    DrawUI(state);

    EndDrawing();
}

// Main loop
void App_Update(AppState* state) {
    UpdateUI(state);
    HandleCanvasInput(state);
}

// Cleanup
void App_Close(AppState* state) {
    Canvas_Destroy(&state->canvas);
    UnloadTexture(state->brushTexture);
    UnloadShader(state->brushShader);
    CloseWindow();
}

// UIButton implementation
void UIButton_Update(UIButton* btn, Vector2 mousePos, bool mousePressed) {
    btn->hovered = CheckCollisionPointRec(mousePos, btn->rect);
    if (btn->hovered && mousePressed && !btn->clicked) {
        btn->clicked = true;
    }
}

void UIButton_Draw(UIButton* btn) {
    Color drawColor = btn->hovered ? btn->hoverColor : btn->color;
    DrawRectangleRec(btn->rect, drawColor);
    DrawRectangleLinesEx(btn->rect, 1, DARKGRAY);

    int textX = btn->rect.x + (btn->rect.width - MeasureText(btn->label, 16)) / 2;
    int textY = btn->rect.y + (btn->rect.height - 16) / 2;
    DrawText(btn->label, textX, textY, 16, WHITE);
}

// UISlider implementation
void UISlider_Update(UISlider* slider, Vector2 mousePos, bool mousePressed) {
    Rectangle sliderArea = {slider->rect.x, slider->rect.y, slider->rect.width, slider->rect.height};
    bool hovered = CheckCollisionPointRec(mousePos, sliderArea);

    if (hovered && mousePressed) {
        slider->dragging = true;
    }
    if (!mousePressed) {
        slider->dragging = false;
    }

    if (slider->dragging) {
        float relX = (mousePos.x - slider->rect.x) / slider->rect.width;
        slider->value = slider->minValue + relX * (slider->maxValue - slider->minValue);
        slider->value = fmaxf(slider->minValue, fminf(slider->maxValue, slider->value));
    }
}

void UISlider_Draw(UISlider* slider) {
    // Draw background
    DrawRectangleRec(slider->rect, slider->color);

    // Draw slider fill
    float fillWidth = ((slider->value - slider->minValue) / (slider->maxValue - slider->minValue)) * slider->rect.width;
    Rectangle fillRect = {slider->rect.x, slider->rect.y, fillWidth, slider->rect.height};
    DrawRectangleRec(fillRect, slider->sliderColor);

    // Draw border
    DrawRectangleLinesEx(slider->rect, 1, DARKGRAY);

    // Draw label
    DrawText(slider->label, slider->rect.x, slider->rect.y - 16, 14, WHITE);

    // Draw value
    char valueText[32];
    sprintf(valueText, "%.2f", slider->value);
    DrawText(valueText, slider->rect.x + slider->rect.width + 5, slider->rect.y + 8, 12, LIGHTGRAY);
}

int main() {
    AppState state = {0};

    App_Init(&state);

    while (!WindowShouldClose()) {
        App_Update(&state);
        App_Draw(&state);
    }

    App_Close(&state);

    return 0;
}
