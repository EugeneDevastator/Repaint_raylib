#include "StrokeThrottle.h"
#include "layerstack.h"
#include "brush_blend.h"
#include "repaint.h"
#include "texture_manager.h"
#include "raylib.h"

StrokeThrottle* g_throttle = nullptr;

void StrokeThrottle::Push(const SegmentData& seg) {
    int next = (m_segTail + 1) % SEG_CAP;
    if (next == m_segHead) return;
    m_segQ[m_segTail] = seg;
    m_segTail = next;
}

int StrokeThrottle::DrawPending(AppState* state) {
    double tStart = GetTime();
    int drawn = 0;
    int pixelBudget = m_dynamicBudget;
    while (pixelBudget > 0) {
        if (m_dabIdx >= m_dabCount) {
            if (m_segHead == m_segTail) break;
            SegmentData& seg = m_segQ[m_segHead];
            m_targetSlot = seg.targetSlot;
            m_userTexBucket = seg.userTexBucket;
            m_userTexSlot = seg.userTexSlot;
            m_seamless = seg.seamless != 0;
            m_pixelPerfect = seg.pixelPerfect != 0;
            if (seg.isStrokeStart) m_hasPrevAngle = false;
            if (m_hasPrevAngle)
                seg.brushFrom.resangle = m_lastSegEndAngle;

            SegResult r;
            m_dabCount = DrawLinear(seg, 0, 0.0f, m_dabBuf, DAB_CAP, &r);

            if (m_dabCount > 0) {
                if (!m_hasPrevAngle)
                    m_dabBuf[0].srcAngle = m_dabBuf[0].brush.resangle;
                m_lastSegEndAngle = m_dabBuf[m_dabCount - 1].brush.resangle;
                m_hasPrevAngle = true;
            }

            if (m_dabCount >= DAB_CAP)
                printf("[THR] WARNING: dabs hit DAB_CAP (%d)!\n", DAB_CAP);
            m_dabIdx = 0;
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
            TexSlot* ts = TM_Get(m_targetSlot);
            if (ts && ts->rt.id > 0) {
                rt = ts->rt;
                TexSlotID btId = {m_userTexBucket, m_userTexSlot};
                TexSlot* bts = TM_Get(btId);
                if (bts) {
                    brushTex = bts->rt.texture;
                    useTexture = true;
                }
            }

            if (rt.id > 0)
                BrushBlend_ApplyStamp(rt, pt.brush, brushTex, useTexture,
                    pt.x, pt.y, pt.srcX, pt.srcY, pt.srcRad, pt.srcAngle,
                    m_seamless, m_pixelPerfect);

            pixelBudget -= cost;
            m_dabIdx++;
            drawn++;
            frameDabs++;
        }
        if (frameDabs == 0) break;
    }

    if (drawn > 0) {
        double elapsed = GetTime() - tStart;
        if (elapsed > m_targetFrameTime * 1.2)
            m_dynamicBudget = (int)(m_dynamicBudget * 0.85);
        else if (elapsed < m_targetFrameTime * 0.5)
            m_dynamicBudget = (int)(m_dynamicBudget * 1.15);
        if (m_dynamicBudget < m_minBudget) m_dynamicBudget = m_minBudget;
        if (m_dynamicBudget > m_maxBudget) m_dynamicBudget = m_maxBudget;
    }

    return drawn;
}
