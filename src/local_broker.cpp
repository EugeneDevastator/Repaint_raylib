#include "repaint.h"

LocalBroker::LocalBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void LocalBroker::on_input(const InputEvent& e) {
    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;
    if (!appState) return;

    d_Brush* br = &appState->currentBrush;
    int layer = appState->activeLayer;

    queue[tail].x = e.x;
    queue[tail].y = e.y;
    queue[tail].srcX = e.srcX;
    queue[tail].srcY = e.srcY;
    queue[tail].color = Color{
        (uint8_t)((e.color >> 16) & 0xFF),
        (uint8_t)((e.color >> 8) & 0xFF),
        (uint8_t)(e.color & 0xFF),
        (uint8_t)((e.color >> 24) & 0xFF)
    };
    queue[tail].rad_out = br->Realb.rad_out;
    queue[tail].rad_in = br->Realb.rad_in;
    queue[tail].opacity = br->Realb.opacity;
    queue[tail].crv = br->Realb.crv;
    queue[tail].x2y = br->Realb.x2y;
    queue[tail].sol = br->Realb.sol;
    queue[tail].sol2op = br->Realb.sol2op;
    queue[tail].resangle = (float)br->Realb.resangle;
    queue[tail].cop = br->Realb.cop;
    queue[tail].bmidx = (int)br->Realb.bmidx;
    queue[tail].seed = br->Realb.seed;
    queue[tail].activeLayer = layer;
    queue[tail].targetRT = appState->layerRTs[layer];

    tail = next;
}

void LocalBroker::poll(AppState* state) {
    while (head != tail) {
        QueuedDab* d = &queue[head];

        if (d->targetRT.id != 0 && d->activeLayer >= 0 && d->activeLayer < state->texCount) {
            d_Brush brush = {};
            brush.Realb.rad_in = d->rad_in;
            brush.Realb.rad_out = d->rad_out;
            brush.Realb.opacity = d->opacity;
            brush.Realb.crv = d->crv;
            brush.Realb.x2y = d->x2y;
            brush.Realb.sol = d->sol;
            brush.Realb.sol2op = d->sol2op;
            brush.Realb.resangle = d->resangle;
            brush.Realb.cop = d->cop;
            brush.Realb.bmidx = (uint8_t)d->bmidx;
            brush.Realb.seed = d->seed;
            brush.Realb.col = d->color;

            RenderTexture2D rt = state->layerRTs[d->activeLayer];
            if (rt.id > 0)
                BrushBlend_ApplyStamp(rt, &brush, d->x, d->y, d->srcX, d->srcY);
        }

        head = (head + 1) % CMD_CAPACITY;
    }
}
