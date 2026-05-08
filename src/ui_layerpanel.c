#include "repaint.h"
#include "rlgl.h"

UISlider layerOpSlider;

int dragFromIdx = -1;
bool dragActive = false;
Vector2 dragMouseDownPos = {0, 0};
int dragDropTarget = -1;

static void DrawLayerEntry(AppState* state, int idx, int x, int y) {
    Rectangle entryRect = {(float)x + 4, (float)y, (float)RIGHT_PANEL_WIDTH - 8, (float)LAYER_ENTRY_H};

    bool isActive = (idx == state->activeLayer);
    Color bg = isActive ? (Color){70, 70, 85, 255} : (Color){50, 50, 55, 255};
    DrawRectangleRec(entryRect, bg);
    if (isActive)
        DrawRectangleLinesEx(entryRect, 1, (Color){100, 140, 200, 255});

    Rectangle checkRect = {(float)x + 10, (float)(y + LAYER_ENTRY_H / 2 - 10), 20, 20};
    DrawRectangleRec(checkRect, (Color){40, 40, 45, 255});
    DrawRectangleLinesEx(checkRect, 1, (Color){100, 100, 110, 255});
    if (state->canvas.layerProps[idx].visible) {
        DrawText("V", (int)checkRect.x + 6, (int)checkRect.y + 4, 12, GREEN);
    }

    Rectangle thumbRect = {(float)x + 36, (float)y + 4, 44, 44};
    {
        int cw = 8, ch = 8;
        for (int ty = 0; ty < 44; ty += ch) {
            for (int tx = 0; tx < 44; tx += cw) {
                bool light = ((tx / cw) + (ty / ch)) % 2 == 0;
                DrawRectangle((int)thumbRect.x + tx, (int)thumbRect.y + ty, cw, ch,
                              light ? (Color){70, 70, 75, 255} : (Color){55, 55, 60, 255});
            }
        }
        DrawRectangleLinesEx(thumbRect, 1, (Color){80, 80, 90, 255});
    }

    if (idx < state->texCount && state->layerTextures && state->layerTextures[idx].id > 0) {
        rlSetBlendMode(RL_BLEND_ALPHA_PREMULTIPLY);
        float scale = fminf(44.0f / state->canvas.width, 44.0f / state->canvas.height);
        Rectangle src = {0, 0, (float)state->canvas.width, (float)state->canvas.height};
        Rectangle dst = {(float)x + 36, (float)y + 4, state->canvas.width * scale, state->canvas.height * scale};
        DrawTexturePro(state->layerTextures[idx], src, dst, (Vector2){0, 0}, 0, WHITE);
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    char lname[32];
    sprintf(lname, "Layer %d", idx + 1);
    DrawText(lname, x + 86, y + LAYER_ENTRY_H / 2 - 6, 12, isActive ? WHITE : LIGHTGRAY);
}

void LayerPanel_HandleInput(AppState* state, Vector2 mousePos) {
    int rpx = RIGHT_PANEL_X;
    int maxVisible = (SCREEN_HEIGHT - 180) / LAYER_ENTRY_H;
    int layerCount = state->canvas.layerCount;
    int listTop = 130;
    int listBottom = listTop + layerCount * LAYER_ENTRY_H;

    Rectangle addRect = {(float)rpx + 10, 40, 80, 36};
    Rectangle delRect = {(float)rpx + 100, 40, 80, 36};
    if (CheckCollisionPointRec(mousePos, addRect) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        Canvas_InsertLayer(&state->canvas, state->activeLayer + 1);
        SyncAllRTs(state);
        layersDirty = true;
    }
    if (CheckCollisionPointRec(mousePos, delRect) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (state->canvas.layerCount > 1) {
            int del = state->activeLayer;
            Canvas_DeleteLayer(&state->canvas, del);
            if (state->activeLayer >= state->canvas.layerCount)
                state->activeLayer = state->canvas.layerCount - 1;
            layersDirty = true;
        }
    }

    if (!dragActive) {
        for (int i = 0; i < layerCount && i < maxVisible; i++) {
            int aIdx = layerCount - 1 - i;
            Rectangle entryRect = {(float)rpx + 4, (float)(listTop + i * LAYER_ENTRY_H), (float)RIGHT_PANEL_WIDTH - 8, (float)LAYER_ENTRY_H};
            Rectangle checkRect = {(float)rpx + 10, (float)(listTop + i * LAYER_ENTRY_H + LAYER_ENTRY_H / 2 - 10), 20, 20};

            if (CheckCollisionPointRec(mousePos, entryRect) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (!CheckCollisionPointRec(mousePos, checkRect)) {
                    dragFromIdx = aIdx;
                    dragMouseDownPos = mousePos;
                    dragActive = false;
                    dragDropTarget = -1;
                    state->activeLayer = aIdx;
                } else {
                    state->canvas.layerProps[aIdx].visible = !state->canvas.layerProps[aIdx].visible;
                    layersDirty = true;
                }
            }
        }
    }

    if (dragFromIdx >= 0 && !dragActive) {
        float dx = mousePos.x - dragMouseDownPos.x;
        float dy = mousePos.y - dragMouseDownPos.y;
        if (dx * dx + dy * dy > 36.0f) {
            dragActive = true;
        }
    }

    if (dragActive) {
        int maxTarget = layerCount;
        if (mousePos.y < listTop) {
            dragDropTarget = 0;
        } else if (mousePos.y >= listBottom) {
            dragDropTarget = maxTarget;
        } else {
            int overIdx = (int)((mousePos.y - listTop) / LAYER_ENTRY_H);
            if (overIdx < 0) overIdx = 0;
            if (overIdx >= layerCount) overIdx = layerCount - 1;
            float midY = listTop + overIdx * LAYER_ENTRY_H + LAYER_ENTRY_H / 2;
            dragDropTarget = (mousePos.y < midY) ? overIdx : overIdx + 1;
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (dragDropTarget >= 0) {
                int to = (dragDropTarget >= layerCount) ? 0 : layerCount - 1 - dragDropTarget;
                if (to >= state->canvas.layerCount) to = state->canvas.layerCount - 1;
                if (to != dragFromIdx) {
                    int prevActive = state->activeLayer;
                    if (prevActive == dragFromIdx) {
                        state->activeLayer = to;
                    } else if (dragFromIdx < to && prevActive > dragFromIdx && prevActive <= to) {
                        state->activeLayer = prevActive - 1;
                    } else if (dragFromIdx > to && prevActive >= to && prevActive < dragFromIdx) {
                        state->activeLayer = prevActive + 1;
                    }
                    int f = dragFromIdx;
                    Canvas_MoveLayer(&state->canvas, f, to);
                    SyncAllRTs(state);
                    layersDirty = true;
                }
            }
            dragFromIdx = -1;
            dragActive = false;
            dragDropTarget = -1;
        }

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            dragFromIdx = -1;
            dragActive = false;
            dragDropTarget = -1;
        }
    }

    if (dragFromIdx >= 0 && !dragActive && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        dragFromIdx = -1;
    }

    UISlider_HandleInput(&layerOpSlider, mousePos);
    float prevOp = state->canvas.layerProps[state->activeLayer].op;
    state->canvas.layerProps[state->activeLayer].op = layerOpSlider.clipmaxF;
    if (state->canvas.layerProps[state->activeLayer].op != prevOp)
        layersDirty = true;

    if (gizmoShow) {
        int gcx = UI_PANEL_WIDTH + (RIGHT_PANEL_X - UI_PANEL_WIDTH) / 2;
        int gcy = SCREEN_HEIGHT / 2;
        int gizR = 100;
        sliderHue.rect = (Rectangle){(float)gcx - 256, (float)gcy + gizR + 48, 512, 24};
        sliderSat.rect = (Rectangle){(float)gcx - 256, (float)gcy + gizR + 80, 512, 24};
        sliderLit.rect = (Rectangle){(float)gcx - 256, (float)gcy + gizR + 112, 512, 24};
    } else {
        sliderHue.rect = (Rectangle){-200, -200, 1, 1};
        sliderSat.rect = (Rectangle){-200, -200, 1, 1};
        sliderLit.rect = (Rectangle){-200, -200, 1, 1};
    }
    UISlider_HandleInput(&sliderHue, mousePos);
    UISlider_HandleInput(&sliderSat, mousePos);
    UISlider_HandleInput(&sliderLit, mousePos);
    colorHue = sliderHue.clipmaxF;
    colorSat = sliderSat.clipmaxF;
    colorLit = sliderLit.clipmaxF;
}

void LayerPanel_Draw(AppState* state) {
    int rpx = RIGHT_PANEL_X;
    Vector2 mp = GetMousePosition();

    DrawRectangle(rpx, 0, RIGHT_PANEL_WIDTH, SCREEN_HEIGHT, (Color){45, 45, 50, 255});
    DrawRectangle(rpx, 0, 1, SCREEN_HEIGHT, DARKGRAY);

    DrawText("Layers", rpx + 10, 10, 20, LIGHTGRAY);

    Rectangle addRect = {(float)rpx + 10, 40, 80, 36};
    Rectangle delRect = {(float)rpx + 100, 40, 80, 36};

    Color addCol = CheckCollisionPointRec(mp, addRect) ? (Color){70, 80, 70, 255} : (Color){55, 65, 55, 255};
    Color delCol = CheckCollisionPointRec(mp, delRect) ? (Color){80, 60, 60, 255} : (Color){65, 55, 55, 255};

    DrawRectangleRec(addRect, addCol);
    DrawRectangleLinesEx(addRect, 1, DARKGRAY);
    DrawText("+ Add", (int)addRect.x + 16, (int)addRect.y + 10, 14, WHITE);

    DrawRectangleRec(delRect, delCol);
    DrawRectangleLinesEx(delRect, 1, DARKGRAY);
    DrawText("- Del", (int)delRect.x + 16, (int)delRect.y + 10, 14, WHITE);

    layerOpSlider.rect = (Rectangle){(float)rpx + 10, 86, 180, 24};
    layerOpSlider.clipmaxF = state->canvas.layerProps[state->activeLayer].op;
    UISlider_Draw(&layerOpSlider);

    int maxVisible = (SCREEN_HEIGHT - 180) / LAYER_ENTRY_H;
    int listTop = 130;
    int layerCount = state->canvas.layerCount;
    for (int i = 0; i < layerCount && i < maxVisible; i++) {
        DrawLayerEntry(state, layerCount - 1 - i, rpx, listTop + i * LAYER_ENTRY_H);
    }

    if (dragActive && dragDropTarget >= 0) {
        int indicatorY = listTop + dragDropTarget * LAYER_ENTRY_H;
        int maxY = listTop + (layerCount > maxVisible ? maxVisible : layerCount) * LAYER_ENTRY_H;
        if (indicatorY >= listTop && indicatorY <= maxY) {
            DrawRectangle(rpx + 4, indicatorY - 4, RIGHT_PANEL_WIDTH - 8, 8, (Color){100, 200, 255, 180});
        }
    }
}
