#include "StrokeThrottle.h"
#include "layerstack.h"
#include "brush_blend.h"
#include "repaint.h"

StrokeThrottle* g_throttle = nullptr;

void StrokeThrottle::Push(const SegmentData& seg) {
    int next = (m_segTail + 1) % SEG_CAP;
    if (next == m_segHead) return;
    m_segQ[m_segTail] = seg;
    m_segTail = next;
}

int StrokeThrottle::DrawPending(AppState* state, int pixelBudget) {
    int drawn = 0;
    while (pixelBudget > 0) {
        if (m_dabIdx >= m_dabCount) {
            if (m_segHead == m_segTail) break;
            SegmentData& seg = m_segQ[m_segHead];
            m_targetType = seg.targetType;
            m_targetId = seg.targetId;
            m_userTexIdx = seg.userTexIdx;
            m_seamless = seg.seamless != 0;
            m_pixelPerfect = seg.pixelPerfect != 0;
            SegResult r;
            m_dabCount = DrawLinear(seg, 0, 0.0f, m_dabBuf, DAB_CAP, &r);
            printf("[THR] unpacked seg: %d dabs, rad=%.1f, spacing=%.2f\n",
                m_dabCount, seg.brushFrom.rad_out_px, seg.brushFrom.spacing);
            fflush(stdout);
            m_dabIdx = 0;
            if (m_dabCount >= DAB_CAP)
                printf("[THR] WARNING: dabs hit DAB_CAP (%d)!\n", DAB_CAP);
            m_segHead = (m_segHead + 1) % SEG_CAP;
        }

        int frameDabs = 0;
        while (m_dabIdx < m_dabCount && pixelBudget > 0) {
            DabPoint& pt = m_dabBuf[m_dabIdx];
            float r = pt.brush.rad_out_px;
            if (r < 0.5f) r = 0.5f;
            int cost = (int)(r * r);
            if (cost > pixelBudget) break;

            RenderTexture2D rt = {0};
            Texture2D brushTex = {0};
            bool useTexture = false;
            if (m_targetType == 1) {
                if (m_targetId < state->brushTexCount && state->brushTex[m_targetId].rt.id > 0)
                    rt = state->brushTex[m_targetId].rt;
            } else {
                if (m_targetId < LayerStack_Count()) {
                    rt = LayerStack_GetRT(m_targetId);
                    if (m_userTexIdx > 0 && (m_userTexIdx - 1u) < (uint8_t)state->brushTexCount) {
                        brushTex = state->brushTex[m_userTexIdx - 1].rt.texture;
                        useTexture = true;
                    }
                }
            }

            if (rt.id > 0)
                BrushBlend_ApplyStamp(rt, pt.brush, brushTex, useTexture,
                    pt.x, pt.y, pt.srcX, pt.srcY, m_seamless, m_pixelPerfect);

            pixelBudget -= cost;
            m_dabIdx++;
            drawn++;
            frameDabs++;
        }
        if (frameDabs == 0) break;
        printf("[THR] frame: drew %d dabs, budget left=%d, remaining in seg=%d\n",
            frameDabs, pixelBudget, m_dabCount - m_dabIdx);
    }
    return drawn;
}
