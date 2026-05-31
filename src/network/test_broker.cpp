#include "test_broker.h"

TestBroker g_testBroker;
bool g_useTestBroker = false;

TestBroker::TestBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void TestBroker::on_segment(const DrawSegment& seg) {
    if (!appState) return;
    int layer = appState->activeLayer;
    if (layer < 0 || layer >= LayerStack_Count()) return;

    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].activeLayer = layer;
    tail = next;

    // Duplicate with +200px X offset
    next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x + 200.0f;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x + 200.0f;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].activeLayer = layer;
    tail = next;
}

void TestBroker::poll(AppState* state) {
    while (head != tail) {
        Dab* d = &queue[head];
        int layer = d->activeLayer;
        if (layer < 0 || layer >= LayerStack_Count()) {
            head = (head + 1) % CMD_CAPACITY;
            continue;
        }
        RenderTexture2D rt = LayerStack_GetRT(layer);
        if (rt.id > 0) {
            d_Brush brush = {};
            brush.Realb = d->brush;
            BrushBlend_ApplyStamp(rt, &brush, g_activeBrushTex, d->x, d->y, d->srcX, d->srcY);
        }
        head = (head + 1) % CMD_CAPACITY;
    }
}
