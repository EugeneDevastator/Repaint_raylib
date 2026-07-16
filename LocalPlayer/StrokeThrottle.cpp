#include "StrokeThrottle.h"
#include "layerstack.h"
#include "brush_blend.h"
#include "repaint.h"
#include "texture_manager.h"
#include "raylib.h"

extern bool g_useV2;

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
            m_layerWW = seg.layerWW;
            m_layerWH = seg.layerWH;
            m_worldToTexPx = seg.worldToTexPx;
            // Angle/smudge carry-over removed — the emitter already provides
            // continuous per-segment resangle and radius via m_modulated continuity.
            if (m_hasPrevSmudge) {
                seg.smudgeSrcX = m_prevSmudgeSrcX;
                seg.smudgeSrcY = m_prevSmudgeSrcY;
            }

            SegResult r;
            extern bool g_useV2;
            if (g_useV2)
                m_dabCount = BuildSegment(seg, 0, 0.0f, m_dabBuf, DAB_CAP, &r);
            else
                m_dabCount = DrawLinear_old(seg, 0, 0.0f, m_dabBuf, DAB_CAP, &r);

            m_prevSmudgeSrcX = r.lastSmudgeSrc.x;
            m_prevSmudgeSrcY = r.lastSmudgeSrc.y;
            m_hasPrevSmudge = m_dabCount > 0;

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

            if (rt.id > 0) {
                if (g_useV2)
                    RasterizeDab(rt, m_worldToTexPx, pt,
                                 brushTex, useTexture, m_seamless, m_pixelPerfect);
                else
                    BrushBlend_ApplyStamp(rt, pt.brush, brushTex, useTexture,
                        pt.x, pt.y, pt.srcX, pt.srcY, pt.srcRad, pt.srcAngle,
                        m_seamless, m_pixelPerfect);
            }

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

