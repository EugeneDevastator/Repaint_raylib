#include "repaint.h"

LocalBroker::LocalBroker() {
    head = 0;
    tail = 0;
    appState = NULL;
}

void LocalBroker::on_input(const BrushDab& e) {
    int next = (tail + 1) % CMD_CAPACITY;
    if (next == head) return;
    if (!appState) return;

    int layer = appState->activeLayer;

    queue[tail].x = e.x;
    queue[tail].y = e.y;
    queue[tail].srcX = e.srcX;
    queue[tail].srcY = e.srcY;
    queue[tail].color      = e.brush.col;
    queue[tail].rad_out    = e.brush.rad_out;
    queue[tail].radInRatio = e.brush.radInRatio;
    queue[tail].opacity    = e.brush.opacity;
    queue[tail].crv        = e.brush.crv;
    queue[tail].x2y        = e.brush.x2y;
    queue[tail].sol        = e.brush.sol;
    queue[tail].sol2op     = e.brush.sol2op;
    queue[tail].resangle   = (float)e.brush.resangle;
    queue[tail].cop        = e.brush.cop;
    queue[tail].texBlendVal  = e.brush.texBlendVal;
    queue[tail].texScale     = e.brush.texScale;
    queue[tail].texFeather   = e.brush.texFeather;
    queue[tail].texThresh    = e.brush.texThresh;
    queue[tail].useTexLumAsAlpha = e.brush.useTexLumAsAlpha;
    queue[tail].texUseRGB    = e.brush.texUseRGB;
    queue[tail].texBlendMode = e.brush.texBlendMode;
    queue[tail].texNoisemode = e.brush.texNoisemode;
    queue[tail].texColorMode = e.brush.texColorMode;
    queue[tail].bmidx      = (int)e.brush.bmidx;
    queue[tail].seed       = e.brush.seed;
    queue[tail].preserveop = e.brush.preserveop;
    queue[tail].eraseMode  = e.brush.eraseMode;
    queue[tail].perspective = e.brush.perspective;
    queue[tail].userTexOriginX = e.brush.userTexOriginX;
    queue[tail].userTexOriginY = e.brush.userTexOriginY;
    queue[tail].userTexDirection = e.brush.userTexDirection;
    queue[tail].activeLayer = layer;
    queue[tail].targetRT   = LayerStack_GetRT(layer);

    tail = next;
}

void LocalBroker::poll(AppState* state) {
    while (head != tail) {
        QueuedDab* d = &queue[head];

        if (d->targetRT.id != 0 && d->activeLayer >= 0 && d->activeLayer < LayerStack_Count()) {
            d_Brush brush = {};
            brush.Realb.radInRatio = d->radInRatio;
            brush.Realb.rad_out  = d->rad_out;
            brush.Realb.opacity  = d->opacity;
            brush.Realb.crv      = d->crv;
            brush.Realb.x2y      = d->x2y;
            brush.Realb.sol      = d->sol;
            brush.Realb.sol2op   = d->sol2op;
            brush.Realb.resangle = d->resangle;
            brush.Realb.cop        = d->cop;
            brush.Realb.texBlendVal  = d->texBlendVal;
            brush.Realb.texScale     = d->texScale;
            brush.Realb.texFeather   = d->texFeather;
            brush.Realb.texThresh    = d->texThresh;
            brush.Realb.useTexLumAsAlpha = d->useTexLumAsAlpha;
            brush.Realb.texUseRGB    = d->texUseRGB;
            brush.Realb.texBlendMode = d->texBlendMode;
            brush.Realb.texNoisemode = d->texNoisemode;
            brush.Realb.texColorMode = d->texColorMode;
            brush.Realb.bmidx      = (uint8_t)d->bmidx;
            brush.Realb.seed       = d->seed;
            brush.Realb.col        = d->color;
            brush.Realb.preserveop = d->preserveop;
            brush.Realb.eraseMode  = d->eraseMode;
            brush.Realb.perspective = d->perspective;
            brush.Realb.userTexOriginX = d->userTexOriginX;
            brush.Realb.userTexOriginY = d->userTexOriginY;
            brush.Realb.userTexDirection = d->userTexDirection;

            RenderTexture2D rt = LayerStack_GetRT(d->activeLayer);
            if (rt.id > 0)
                BrushBlend_ApplyStamp(rt, &brush, g_activeBrushTex, d->x, d->y, d->srcX, d->srcY);
        }

        head = (head + 1) % CMD_CAPACITY;
    }
}
