#include "DabDrawer.h"
#include "brush_blend.h"
#include "layerstack.h"

DabDrawer* g_dabDrawer = nullptr;
static const int SEGMENT_MAX_OUT = 65536;

void DabDrawer::Push(const DabData& dab) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = dab;
    m_tail = next;
}

bool DabDrawer::PushSafe(const DabData& dab) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return false;
    m_buf[m_tail] = dab;
    m_tail = next;
    return true;
}

void DabDrawer::Clear() {
    m_head = m_tail = 0;
}

int DabDrawer::PendingCount() {
    if (m_tail >= m_head) return m_tail - m_head;
    return CAPACITY - m_head + m_tail;
}

bool DabDrawer::HasRoom(int need) const {
    int used = (m_tail >= m_head) ? (m_tail - m_head) : (CAPACITY - m_head + m_tail);
    return (used + need) < CAPACITY;
}

int DabDrawer::DrawPending(AppState* state, int pixelBudget) {
    int drawn = 0;
    int budget = pixelBudget;
    while (m_head != m_tail && budget > 0) {
        DabData& d = m_buf[m_head];
        float r = d.brush.rad_out_px;
        if (r < 0.5f) r = 0.5f;
        int cost = (int)(r * r);
        if (cost > budget) break;

        RenderTexture2D rt = {0};
        Texture2D brushTex = {0};
        bool useTexture = false;
        if (d.targetType == 1) {
            if (d.targetId < state->brushTexCount && state->brushTex[d.targetId].rt.id > 0) {
                rt = state->brushTex[d.targetId].rt;
            }
        } else {
            if (d.targetId < LayerStack_Count()) {
                rt = LayerStack_GetRT(d.targetId);
                if (d.userTexIdx > 0 && (d.userTexIdx - 1u) < (uint8_t)state->brushTexCount) {
                    brushTex = state->brushTex[d.userTexIdx - 1].rt.texture;
                    useTexture = true;
                }
            }
        }
        if (rt.id > 0)
            BrushBlend_ApplyStamp(rt, d.brush, brushTex, useTexture, d.x, d.y, d.srcX, d.srcY, d.seamless, d.pixelPerfect);

        budget -= cost;
        m_head = (m_head + 1) % CAPACITY;
        drawn++;
    }
    if (m_head == m_tail) m_head = m_tail = 0;
    return drawn;
}

void EmitDabsFromSegment(DabDrawer* dd, const SegmentData& seg, int dabOffset) {
    struct EmitCtx {
        DabDrawer* dd;
        uint8_t targetType;
        uint8_t targetId;
        uint8_t userTexIdx;
        bool seamless;
        bool pixelPerfect;
    };
    EmitCtx ectx = { dd, seg.targetType, seg.targetId, seg.userTexIdx, seg.seamless != 0, seg.pixelPerfect != 0 };

    auto cb = [](float x, float y, float srcX, float srcY,
                 const CollapsedBrush& cbBrush, void* user) {
        EmitCtx* e = (EmitCtx*)user;
        DabData d;
        d.x = x; d.y = y;
        d.srcX = srcX; d.srcY = srcY;
        d.brush = cbBrush;
        d.targetType = e->targetType;
        d.targetId = e->targetId;
        d.userTexIdx = e->userTexIdx;
        d.seamless = e->seamless;
        d.pixelPerfect = e->pixelPerfect;
        e->dd->Push(d);
    };

    SegResult r;
    DrawLinear(seg, dabOffset, 0.0f, cb, &ectx, SEGMENT_MAX_OUT, &r);
}
