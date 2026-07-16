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
            m_layerWW = seg.layerWW;
            m_layerWH = seg.layerWH;
            m_worldToTexPx = seg.worldToTexPx;
            // New stroke: reset carry-over state
            if (seg.isStrokeStart) { m_hasPrevSmudge = false; m_prevResult = SegResult{}; }
            if (m_hasPrevSmudge) CorrectSegmentFromInput(&seg, &m_prevResult);

            SegResult r;
            float chainedRad = m_hasPrevSmudge ? m_prevResult.lastRadOut : 0.0f;
            m_dabCount = BuildSegment(seg, 0, chainedRad, m_dabBuf, DAB_CAP, &r);

            {
                float est = seg.brushFrom.rad_out_px;
                float rTo = seg.brush.rad_out_px;
                float prR = m_prevResult.lastRadOut;
                if (m_dabCount > 0) {
                    float act = m_dabBuf[0].brush.rad_out_px;
                    fprintf(stderr, "SEG cnt=%2d ch=%d est=%.3f act=%.3f rTo=%.3f prR=%.3f jit=%.3f sp=%.3f len=%.1f\n",
                        m_dabCount, m_hasPrevSmudge ? 1 : 0,
                        est, act, rTo, prR,
                        seg.brushFrom.jitRadOut, seg.brushFrom.spacing,
                        sqrtf((seg.pos2.x-seg.pos1.x)*(seg.pos2.x-seg.pos1.x) +
                              (seg.pos2.y-seg.pos1.y)*(seg.pos2.y-seg.pos1.y)));
                } else {
                    fprintf(stderr, "SEG cnt=0  ch=%d est=%.3f rTo=%.3f prR=%.3f sp=%.3f len=%.1f\n",
                        m_hasPrevSmudge ? 1 : 0,
                        est, rTo, prR,
                        seg.brushFrom.spacing,
                        sqrtf((seg.pos2.x-seg.pos1.x)*(seg.pos2.x-seg.pos1.x) +
                              (seg.pos2.y-seg.pos1.y)*(seg.pos2.y-seg.pos1.y)));
                }
            }

            m_prevResult = r;
            m_prevSmudgeSrcX = r.lastSmudgeSrc.x;
            m_prevSmudgeSrcY = r.lastSmudgeSrc.y;
            if (m_dabCount > 0)
                m_hasPrevSmudge = true;
            if (m_dabCount > 0 && m_dbgDabCount + 1 < DBG_PTS) {
                m_dbgDabPos[m_dbgDabCount++] = Vector2{m_dabBuf[0].x, m_dabBuf[0].y};
                m_dbgDabPos[m_dbgDabCount++] = Vector2{m_dabBuf[m_dabCount-1].x, m_dabBuf[m_dabCount-1].y};
            }

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
                RasterizeDab(rt, m_worldToTexPx, pt,
                             brushTex, useTexture, m_seamless, m_pixelPerfect);
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

