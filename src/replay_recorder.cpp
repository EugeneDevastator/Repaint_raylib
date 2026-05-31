#include "replay_recorder.h"
#include "layerstack.h"
#include <string.h>
#include <stdio.h>

#define RPL_MAGIC "RPLREP"
#define RPL_VER 2

void ReplayRecorder::on_segment(const DrawSegment& seg) {
    if (m_playing) return;
    NetSegment s;
    s.pos1 = seg.pos1; s.pos2 = seg.pos2;
    s.ctrl0 = seg.ctrl0; s.ctrl3 = seg.ctrl3;
    s.brushFrom = seg.brushFrom;
    s.brushTo  = seg.brush;
    s.seed = seg.seed;
    s.layer = 0;
    s.toolID = 0;
    m_segs.push_back(s);
    if (m_segs.size() % 10 == 0)
        printf("[REC] %d segments recorded\n", (int)m_segs.size()), fflush(stdout);
}

void ReplayRecorder::poll(AppState* state) {
    if (!m_playing || m_segs.empty()) return;
    printf("[REPLAY] poll: playing %d segments\n", (int)m_segs.size());
    fflush(stdout);
    m_playing = false;
    Play(state);
}

void ReplayRecorder::Reset(int canvasW, int canvasH) {
    printf("[REPLAY] Reset: %dx%d\n", canvasW, canvasH);
    fflush(stdout);
    m_segs.clear();
    m_canvasW = canvasW;
    m_canvasH = canvasH;
    m_playing = false;
}

void ReplayRecorder::Play(AppState* state) {
    if (m_segs.empty()) return;

    if (!state) { printf("[REPLAY] Play: state is NULL!\n"); fflush(stdout); return; }
    printf("[REPLAY] Play: %d segs, canvas=%dx%d, doc=%dx%d, layer=%d\n",
        (int)m_segs.size(), m_canvasW, m_canvasH,
        state->doc.width, state->doc.height, state->activeLayer);
    fflush(stdout);

    float sx = (float)state->doc.width  / (float)(m_canvasW > 0 ? m_canvasW : 512);
    float sy = (float)state->doc.height / (float)(m_canvasH > 0 ? m_canvasH : 512);
    printf("[REPLAY] sx=%.3f sy=%.3f\n", sx, sy); fflush(stdout);

    RenderTexture2D rt = LayerStack_GetRT(state->activeLayer);
    if (rt.id == 0) { printf("[REPLAY] RT invalid\n"); fflush(stdout); return; }

    int segIdx = 0;
    for (auto& ns : m_segs) {
        segIdx++;
        DrawSegment dseg;
        memset(&dseg, 0, sizeof(dseg));
        dseg.pos1.x = ns.pos1.x * sx; dseg.pos1.y = ns.pos1.y * sy;
        dseg.pos2.x = ns.pos2.x * sx; dseg.pos2.y = ns.pos2.y * sy;
        dseg.ctrl0.x = ns.ctrl0.x * sx; dseg.ctrl0.y = ns.ctrl0.y * sy;
        dseg.ctrl3.x = ns.ctrl3.x * sx; dseg.ctrl3.y = ns.ctrl3.y * sy;
        dseg.brushFrom = ns.brushFrom;
        dseg.brush     = ns.brushTo;
        if (dseg.brushFrom.spacing <= 0.0f) dseg.brushFrom.spacing = 0.5f;
        if (dseg.brush.spacing <= 0.0f) dseg.brush.spacing = 0.5f;
        dseg.seed = ns.seed;
        dseg.Noisemode = 0;

        printf("[REPLAY] seg %d: pos1=%.1f,%.1f pos2=%.1f,%.1f rad=%.1f spacing=%.2f\n",
            segIdx, dseg.pos1.x, dseg.pos1.y, dseg.pos2.x, dseg.pos2.y,
            dseg.brushFrom.rad_out_px, dseg.brushFrom.spacing);
        fflush(stdout);

        DrawDab dabs[512];
        SegResult r;
        int n = DrawLinear(&dseg, 0, 0.0f, dabs, 512, &r);
        printf("[REPLAY] seg %d: %d dabs\n", segIdx, n); fflush(stdout);
        for (int i = 0; i < n; i++) {
            CollapsedBrush* cb = &dabs[i].brush;
            d_Brush tb; memset(&tb, 0, sizeof(tb));
            tb.Realb.rad_out = cb->rad_out_px;
            tb.Realb.radInRatio = cb->radInRatio;
            tb.Realb.opacity = cb->opacity;
            tb.Realb.crv = cb->crv;
            tb.Realb.x2y = cb->scale_y;
            tb.Realb.resangle = cb->resangle;
            tb.Realb.col = cb->col;
            tb.Realb.cop = cb->cop;
            tb.Realb.bmidx = (uint8_t)cb->bmidx;
            tb.Realb.preserveop = cb->preserveop;
            tb.Realb.eraseMode = cb->eraseMode;
            tb.Realb.perspective = cb->perspective;
            tb.Realb.texScale = cb->texScale;
            tb.Realb.texFeather = cb->texFeather;
            tb.Realb.texThresh = cb->texThresh;
            tb.Realb.texBlendVal = cb->texBlendVal;
            tb.Realb.texBlendMode = cb->texBlendMode;
            tb.Realb.texNoisemode = cb->texNoisemode;
            tb.Realb.texColorMode = cb->texColorMode;
            tb.Realb.useTexLumAsAlpha = cb->useTexLumAsAlpha;
            tb.Realb.pwr = cb->pwr;
            tb.Realb.userTexOriginX = cb->userTexOriginX;
            tb.Realb.userTexOriginY = cb->userTexOriginY;
            tb.Realb.userTexDirection = cb->userTexDirection;
            RenderTexture2D rt = LayerStack_GetRT(state->activeLayer);
            if (rt.id > 0)
                BrushBlend_ApplyStamp(rt, &tb, g_activeBrushTex, dabs[i].x, dabs[i].y, dabs[i].srcX, dabs[i].srcY);
        }
    }
    printf("[REPLAY] Play done\n"); fflush(stdout);
}

bool ReplayRecorder::Save(const char* path) {
    printf("[RPSAVE] %s: %d segs\n", path, (int)m_segs.size()); fflush(stdout);
    if (m_segs.empty()) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    fwrite(RPL_MAGIC, 1, 6, f);
    uint32_t ver = RPL_VER;
    fwrite(&ver, 4, 1, f);
    uint32_t w = (uint32_t)m_canvasW, h = (uint32_t)m_canvasH;
    fwrite(&w, 4, 1, f);
    fwrite(&h, 4, 1, f);
    uint32_t count = (uint32_t)m_segs.size();
    fwrite(&count, 4, 1, f);

    for (auto& ns : m_segs) {
        fwrite(&ns.pos1, sizeof(Vector2), 1, f);
        fwrite(&ns.pos2, sizeof(Vector2), 1, f);
        fwrite(&ns.ctrl0, sizeof(Vector2), 1, f);
        fwrite(&ns.ctrl3, sizeof(Vector2), 1, f);
        fwrite(&ns.brushFrom, sizeof(CollapsedBrush), 1, f);
        fwrite(&ns.brushTo, sizeof(CollapsedBrush), 1, f);
        fwrite(&ns.seed, sizeof(uint16_t), 1, f);
        fwrite(&ns.toolID, sizeof(uint8_t), 1, f);
    }
    fclose(f);
    return true;
}

bool ReplayRecorder::Load(const char* path) {
    printf("[REPLAY] Load: %s\n", path); fflush(stdout);
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[REPLAY] fopen failed\n"); fflush(stdout); return false; }

    char magic[6];
    if (fread(magic, 1, 6, f) != 6 || memcmp(magic, RPL_MAGIC, 6) != 0) {
        printf("[REPLAY] bad magic\n"); fflush(stdout);
        fclose(f); return false;
    }
    uint32_t ver;
    if (fread(&ver, 4, 1, f) != 1 || ver != RPL_VER) {
        printf("[REPLAY] bad ver: %d\n", (int)ver); fflush(stdout);
        fclose(f); return false;
    }
    uint32_t w, h, count;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 || fread(&count, 4, 1, f) != 1)
        { printf("[REPLAY] bad header read\n"); fflush(stdout); fclose(f); return false; }
    if (w == 0 || h == 0 || count == 0) { printf("[REPLAY] zero size/count\n"); fflush(stdout); fclose(f); return false; }

    printf("[REPLAY] header: %dx%d, %d segs\n", (int)w, (int)h, (int)count); fflush(stdout);

    m_canvasW = (int)w;
    m_canvasH = (int)h;

    for (uint32_t i = 0; i < count; i++) {
        NetSegment ns;
        if (fread(&ns.pos1, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&ns.pos2, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&ns.ctrl0, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&ns.ctrl3, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&ns.brushFrom, sizeof(CollapsedBrush), 1, f) != 1) break;
        if (fread(&ns.brushTo, sizeof(CollapsedBrush), 1, f) != 1) break;
        if (fread(&ns.seed, sizeof(uint16_t), 1, f) != 1) break;
        if (fread(&ns.toolID, sizeof(uint8_t), 1, f) != 1) break;
        printf("[RPLOAD] seg %d: p1=(%.1f,%.1f) p2=(%.1f,%.1f) rad=%.1f spacing=%.2f col=(%d,%d,%d)\n",
            (int)i, ns.pos1.x, ns.pos1.y, ns.pos2.x, ns.pos2.y,
            ns.brushFrom.rad_out_px, ns.brushFrom.spacing,
            ns.brushFrom.col.r, ns.brushFrom.col.g, ns.brushFrom.col.b);
        fflush(stdout);
        m_segs.push_back(ns);
    }
    printf("[RPLOAD] loaded %d segs total\n", (int)m_segs.size()); fflush(stdout);
    fclose(f);
    return true;
}
