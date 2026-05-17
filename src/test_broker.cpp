#include "test_broker.h"

TestBroker g_testBroker;
bool g_useTestBroker = false;

TestBroker::TestBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void TestBroker::on_input(const InputEvent& e) {
    if (!appState) return;
    int layer = appState->activeLayer;
    if (layer < 0 || layer >= appState->texCount) return;

    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = e.x;
    queue[tail].y = e.y;
    queue[tail].srcX = e.srcX;
    queue[tail].srcY = e.srcY;
    queue[tail].brush = e.brush;
    queue[tail].activeLayer = layer;
    tail = next;

    // Duplicate with +200px X offset
    next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = e.x + 200.0f;
    queue[tail].y = e.y;
    queue[tail].srcX = e.srcX + 200.0f;
    queue[tail].srcY = e.srcY;
    queue[tail].brush = e.brush;
    queue[tail].activeLayer = layer;
    tail = next;
}

void TestBroker::poll(AppState* state) {
    while (head != tail) {
        Dab* d = &queue[head];
        int layer = d->activeLayer;
        if (layer < 0 || layer >= state->texCount) {
            head = (head + 1) % CMD_CAPACITY;
            continue;
        }
        RenderTexture2D rt = state->layerRTs[layer];
        if (rt.id > 0) {
            d_Brush brush = {};
            brush.Realb = d->brush;
            BrushBlend_ApplyStamp(rt, &brush, g_activeBrushTex, d->x, d->y, d->srcX, d->srcY);
        }
        head = (head + 1) % CMD_CAPACITY;
    }
}
