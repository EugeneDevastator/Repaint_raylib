#include "SegmentRenderer.h"

SegmentRenderer* g_segRenderer = nullptr;

void SegmentRenderer::Push(const PendingDraw& d) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = d;
    m_tail = next;
}

int SegmentRenderer::RenderPending(int maxPerFrame) {
    int rendered = 0;
    while (m_head != m_tail && rendered < maxPerFrame) {
        PendingDraw& d = m_buf[m_head];
        if (d.targetRT.id != 0) {
            DrawOneSegment(d.seg, d.targetRT, d.brushTex, d.seamless);
        }
        m_head = (m_head + 1) % CAPACITY;
        rendered++;
    }
    if (m_head == m_tail)
        m_head = m_tail = 0;
    return rendered;
}
