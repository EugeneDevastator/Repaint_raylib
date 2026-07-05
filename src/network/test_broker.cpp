#include "test_broker.h"
#include "repaint.h"
#include "stroke_engine.h"
#include "brush_blend.h"
#include "texture_manager.h"

TestBroker g_testBroker;
bool g_useTestBroker = false;

TestBroker::TestBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void TestBroker::on_segment(const SegmentData& seg) {
    if (!appState) return;

    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].targetSlot = seg.targetSlot;
    queue[tail].userTexBucket = seg.userTexBucket;
    queue[tail].userTexSlot = seg.userTexSlot;
    tail = next;

    next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;

    queue[tail].x = seg.pos1.x + 200.0f;
    queue[tail].y = seg.pos1.y;
    queue[tail].srcX = seg.pos2.x + 200.0f;
    queue[tail].srcY = seg.pos2.y;
    queue[tail].brush = appState->currentBrush.Realb;
    queue[tail].targetSlot = seg.targetSlot;
    queue[tail].userTexBucket = seg.userTexBucket;
    queue[tail].userTexSlot = seg.userTexSlot;
    tail = next;
}

void TestBroker::poll(AppState* state) {
    while (head != tail) {
        Dab* d = &queue[head];

        RenderTexture2D rt = {0};
        TexSlot* ts = TM_Get(d->targetSlot);
        if (ts && ts->rt.id > 0) rt = ts->rt;

        if (rt.id > 0) {
            SetupBrushContext(d->brush, eBrush, 0.0f);
            CollapsedBrush cb = GetCollapsedBrush(g_modPars.Pars);
            Texture2D brushTex = {0};
            bool useTexture = false;
            TexSlotID texId = {d->userTexBucket, d->userTexSlot};
            TexSlot* ts = TM_Get(texId);
            if (ts) {
                brushTex = ts->rt.texture;
                useTexture = true;
            }
            BrushBlend_ApplyStamp(rt, cb, brushTex, useTexture, d->x, d->y, d->srcX, d->srcY, 0.0f, 0.0f, false, false);
        }
        head = (head + 1) % CMD_CAPACITY;
    }
}
