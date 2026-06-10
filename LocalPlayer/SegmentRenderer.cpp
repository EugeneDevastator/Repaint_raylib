#include "SegmentRenderer.h"
#include "repaint.h"
#include "DabDrawer.h"

SegmentRenderer* g_segRenderer = nullptr;

void SegmentRenderer::Push(const SegmentData& seg) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = seg;
    m_tail = next;
}

int SegmentRenderer::EmitPending(int maxSegments, DabDrawer* dd) {
    int emitted = 0;
    while (m_head != m_tail && emitted < maxSegments) {
        if (!dd->HasRoom(DabDrawer::SEGMENT_ESTIMATE)) break;

        SegmentData& seg = m_buf[m_head];
        EmitDabsFromSegment(dd, seg, seg.dabOffset);

        m_head = (m_head + 1) % CAPACITY;
        emitted++;
    }
    if (m_head == m_tail) m_head = m_tail = 0;
    return emitted;
}
