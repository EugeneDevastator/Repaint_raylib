#include "SegmentRenderer.h"
#include "repaint.h"
#include "layerstack.h"

SegmentRenderer* g_segRenderer = nullptr;

void SegmentRenderer::Push(const DrawSegment& seg) {
    int next = (m_tail + 1) % CAPACITY;
    if (next == m_head) return;
    m_buf[m_tail] = seg;
    m_tail = next;
}

int SegmentRenderer::RenderPending(AppState* state, int maxPerFrame) {
    int rendered = 0;
    while (m_head != m_tail && rendered < maxPerFrame) {
        DrawSegment& seg = m_buf[m_head];

        // Resolve render target and brush texture from segment metadata
        RenderTexture2D rt = {0};
        Texture2D brushTex = {0};
        uint8_t tt = seg.targetType;
        uint8_t ti = seg.targetId;

        if (tt == 1) {
            // Brush texture editing
            if (ti < state->brushTexCount && state->brushTex[ti].rt.id > 0) {
                rt = state->brushTex[ti].rt;
                brushTex = g_defaultBrushTex;
            }
        } else {
            // Layer painting
            if (ti < LayerStack_Count()) {
                rt = LayerStack_GetRT(ti);
                brushTex = g_activeBrushTex;
            }
        }

        if (rt.id != 0)
            DrawOneSegment(seg, rt, brushTex, seg.seamless != 0, seg.dabOffset);

        m_head = (m_head + 1) % CAPACITY;
        rendered++;
    }
    if (m_head == m_tail) m_head = m_tail = 0;
    return rendered;
}
