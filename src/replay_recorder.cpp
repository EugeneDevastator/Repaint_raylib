#include "replay_recorder.h"
#include "layerstack.h"
#include <string.h>
#include <stdio.h>

#define RPL_MAGIC "RPLREP"
#define RPL_VER 6

void ReplayRecorder::on_segment(const SegmentData& seg) {
    if (m_playing) return;
    m_segs.push_back(seg);
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
        // Scale positions to document size
        SegmentData dseg = ns;
        dseg.pos1.x *= sx; dseg.pos1.y *= sy;
        dseg.pos2.x *= sx; dseg.pos2.y *= sy;
        dseg.ctrl0.x *= sx; dseg.ctrl0.y *= sy;
        dseg.ctrl3.x *= sx; dseg.ctrl3.y *= sy;
        dseg.smudgeSrcX *= sx;
        dseg.smudgeSrcY *= sy;
        if (dseg.brushFrom.spacing <= 0.0f) dseg.brushFrom.spacing = 0.5f;
        if (dseg.brush.spacing <= 0.0f) dseg.brush.spacing = 0.5f;

        printf("[REPLAY] seg %d: pos1=%.1f,%.1f pos2=%.1f,%.1f rad=%.1f spacing=%.2f\n",
            segIdx, dseg.pos1.x, dseg.pos1.y, dseg.pos2.x, dseg.pos2.y,
            dseg.brushFrom.rad_out_px, dseg.brushFrom.spacing);
        fflush(stdout);

        Texture2D replayTex = {0};
        bool replayUseTex = false;
        if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount) {
            replayTex = state->brushTex[state->activeBrushTex].rt.texture;
            replayUseTex = true;
        }
        DrawOneSegment(dseg, rt, replayTex, replayUseTex, dseg.seamless != 0, 0, dseg.pixelPerfect != 0);
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

    for (auto& seg : m_segs) {
        fwrite(&seg.pos1, sizeof(Vector2), 1, f);
        fwrite(&seg.pos2, sizeof(Vector2), 1, f);
        fwrite(&seg.ctrl0, sizeof(Vector2), 1, f);
        fwrite(&seg.ctrl3, sizeof(Vector2), 1, f);
        fwrite(&seg.brushFrom, sizeof(CollapsedBrush), 1, f);
        fwrite(&seg.brush, sizeof(CollapsedBrush), 1, f);
        fwrite(&seg.seed, sizeof(uint16_t), 1, f);
        fwrite(&seg.tool, sizeof(uint8_t), 1, f);
        fwrite(&seg.seamless, sizeof(uint8_t), 1, f);
        fwrite(&seg.pixelPerfect, sizeof(uint8_t), 1, f);
        fwrite(&seg.smudgeSrcX, sizeof(float), 1, f);
        fwrite(&seg.smudgeSrcY, sizeof(float), 1, f);
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
    if (fread(&ver, 4, 1, f) != 1 || ver < 5 || ver > RPL_VER) {
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
        SegmentData seg;
        memset(&seg, 0, sizeof(seg));
        if (fread(&seg.pos1, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&seg.pos2, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&seg.ctrl0, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&seg.ctrl3, sizeof(Vector2), 1, f) != 1) break;
        if (fread(&seg.brushFrom, sizeof(CollapsedBrush), 1, f) != 1) break;
        if (fread(&seg.brush, sizeof(CollapsedBrush), 1, f) != 1) break;
        if (fread(&seg.seed, sizeof(uint16_t), 1, f) != 1) break;
        if (fread(&seg.tool, sizeof(uint8_t), 1, f) != 1) break;
        if (fread(&seg.seamless, sizeof(uint8_t), 1, f) != 1) break;
        if (ver >= 6) {
            if (fread(&seg.pixelPerfect, sizeof(uint8_t), 1, f) != 1) break;
        } else {
            seg.pixelPerfect = 0;
        }
        if (fread(&seg.smudgeSrcX, sizeof(float), 1, f) != 1) break;
        if (fread(&seg.smudgeSrcY, sizeof(float), 1, f) != 1) break;
        printf("[RPLOAD] seg %d: p1=(%.1f,%.1f) p2=(%.1f,%.1f) rad=%.1f spacing=%.2f col=(%d,%d,%d)\n",
            (int)i, seg.pos1.x, seg.pos1.y, seg.pos2.x, seg.pos2.y,
            seg.brushFrom.rad_out_px, seg.brushFrom.spacing,
            seg.brushFrom.col.r, seg.brushFrom.col.g, seg.brushFrom.col.b);
        fflush(stdout);
        m_segs.push_back(seg);
    }
    printf("[RPLOAD] loaded %d segs total\n", (int)m_segs.size()); fflush(stdout);
    fclose(f);
    return true;
}
