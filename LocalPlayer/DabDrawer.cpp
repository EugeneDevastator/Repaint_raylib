#include "DabDrawer.h"
#include "repaint.h"

DabDrawer* g_dabDrawer = nullptr;
static const int SEGMENT_MAX_OUT = 65536;

void DabDrawer::Push(const DabEntry& dab) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = dab;
    m_tail = next;
}

bool DabDrawer::PushSafe(const DabEntry& dab) {
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

int DabDrawer::DrawPending(int pixelBudget) {
    int drawn = 0;
    int budget = pixelBudget;
    while (m_head != m_tail && budget > 0) {
        DabEntry& d = m_buf[m_head];
        float r = d.brush.rad_out_px;
        if (r < 0.5f) r = 0.5f;
        int cost = (int)(r * r);
        if (cost > budget) break;

        bool savedSeamless = g_seamlessPaint;
        g_seamlessPaint = d.seamless;
        ApplyCollapsedBrush(d.rt, d.brush, d.x, d.y, d.srcX, d.srcY, d.brushTex);
        g_seamlessPaint = savedSeamless;

        budget -= cost;
        m_head = (m_head + 1) % CAPACITY;
    }
    if (m_head == m_tail) m_head = m_tail = 0;
    return drawn;
}

void EmitDabsFromSegment(DabDrawer* dd, const DrawSegment& seg,
    RenderTexture2D rt, Texture2D brushTex, bool seamless, int dabOffset)
{
    struct EmitCtx {
        DabDrawer* dd;
        RenderTexture2D rt;
        Texture2D tex;
        bool seamless;
    };
    EmitCtx ectx = { dd, rt, brushTex, seamless };

    auto cb = [](float x, float y, float srcX, float srcY,
                 const CollapsedBrush& cbBrush, void* user) {
        EmitCtx* e = (EmitCtx*)user;
        DabEntry d;
        d.x = x; d.y = y;
        d.srcX = srcX; d.srcY = srcY;
        d.rt = e->rt;
        d.brush = cbBrush;
        d.brushTex = e->tex;
        d.seamless = e->seamless;
        e->dd->Push(d);
    };

    SegResult r;
    DrawLinear(&seg, dabOffset, 0.0f, cb, &ectx, SEGMENT_MAX_OUT, &r);
}
