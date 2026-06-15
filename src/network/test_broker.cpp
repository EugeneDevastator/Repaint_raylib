#include "test_broker.h"
#include "stroke_engine.h"
#include "brush_blend.h"

TestBroker g_testBroker;
bool g_useTestBroker = false;

TestBroker::TestBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void TestBroker::on_segment(const SegmentData& seg) {
    if (!appState) return;
    int target = (seg.targetType == 1) ? seg.targetId : appState->activeLayer;

    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].targetType = seg.targetType;
    queue[tail].targetId = target;
    queue[tail].userTexIdx = seg.userTexIdx;
    tail = next;

    next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x + 200.0f;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x + 200.0f;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].targetType = seg.targetType;
    queue[tail].targetId = target;
    queue[tail].userTexIdx = seg.userTexIdx;
    tail = next;
}

void TestBroker::poll(AppState* state) {
    while (head != tail) {
        Dab* d = &queue[head];

        RenderTexture2D rt = {0};
        if (d->targetType == 1 && d->targetId >= 0 && d->targetId < state->brushTexCount) {
            rt = state->brushTex[d->targetId].rt;
        } else if (d->targetId >= 0 && d->targetId < LayerStack_Count()) {
            rt = LayerStack_GetRT(d->targetId);
        }

        if (rt.id > 0) {
            CollapsedBrush cb = CollapseBrushParams(d->brush, 0.0f, eBrush);
            Texture2D brushTex = {0};
            bool useTexture = false;
            if (d->userTexIdx > 0 && (d->userTexIdx - 1u) < (uint8_t)state->brushTexCount) {
                brushTex = state->brushTex[d->userTexIdx - 1].rt.texture;
                useTexture = true;
            }
            BrushBlend_ApplyStamp(rt, cb, brushTex, useTexture, d->x, d->y, d->srcX, d->srcY, 0.0f, 0.0f, false, false);
        }
        head = (head + 1) % CMD_CAPACITY;
    }
}
