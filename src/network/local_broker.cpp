#include "repaint.h"

struct LBDab { float x,y, srcX,srcY; d_RealBrush brush; int layer; RenderTexture2D targetRT; };

static LBDab s_queue[4096];
static volatile int s_head = 0, s_tail = 0;

LocalBroker::LocalBroker() { appState = NULL; }

void LocalBroker::on_segment(const DrawSegment& seg) {
    int next = (s_tail + 1) % 4096;
    if (next == s_head) return;
    if (!appState) return;

    LBDab& d = s_queue[s_tail];
    d.x = seg.pos1.x;
    d.y = seg.pos1.y;
    d.srcX = seg.pos2.x;
    d.srcY = seg.pos2.y;

    CollapsedBrush cb = seg.brushFrom;
    d.brush.rad_out     = cb.rad_out_px;
    d.brush.radInRatio  = cb.radInRatio;
    d.brush.opacity     = cb.opacity;
    d.brush.crv         = cb.crv;
    d.brush.x2y         = cb.scale_y;
    d.brush.sol         = 1.0f;
    d.brush.sol2op      = 0.0f;
    d.brush.resangle    = cb.resangle;
    d.brush.col         = cb.col;
    d.brush.cop         = cb.cop;
    d.brush.bmidx       = (uint8_t)cb.bmidx;
    d.brush.preserveop  = cb.preserveop;
    d.brush.eraseMode   = cb.eraseMode;
    d.brush.perspective = cb.perspective;
    d.brush.texScale    = cb.texScale;
    d.brush.texFeather  = cb.texFeather;
    d.brush.texThresh   = cb.texThresh;
    d.brush.texBlendVal = cb.texBlendVal;
    d.brush.texBlendMode = cb.texBlendMode;
    d.brush.texNoisemode = cb.texNoisemode;
    d.brush.texColorMode = cb.texColorMode;
    d.brush.useTexLumAsAlpha = cb.useTexLumAsAlpha;
    d.brush.texUseRGB   = false;
    d.brush.pwr         = cb.pwr;
    d.brush.userTexOriginX = cb.userTexOriginX;
    d.brush.userTexOriginY = cb.userTexOriginY;
    d.brush.userTexDirection = cb.userTexDirection;
    d.brush.seed        = seg.seed;
    d.layer = appState->activeLayer;
    d.targetRT = LayerStack_GetRT(d.layer);
    s_tail = next;
}

void LocalBroker::poll(AppState* state) {
    (void)state;
    while (s_head != s_tail) {
        LBDab& d = s_queue[s_head];
        if (d.targetRT.id != 0 && d.layer >= 0 && d.layer < LayerStack_Count()) {
            RenderTexture2D rt = LayerStack_GetRT(d.layer);
            if (rt.id > 0) {
                float sx = d.srcX, sy = d.srcY;
                d_Brush tb = {};
                tb.Realb = d.brush;
                BrushBlend_ApplyStamp(rt, &tb, g_activeBrushTex, d.x, d.y, sx, sy);
            }
        }
        s_head = (s_head + 1) % 4096;
    }
}
