#include "file_format.h"
#include "repaint.h"
#include <stdlib.h>
#include <string.h>

#define MAGIC      "REPAINT"
#define MAGIC_LEN  8
#define FILE_VER   1

/* ── Write helpers ─────────────────────────────────────────────────────── */

static void _wu32(uint8_t** p, uint32_t v) {
    (*p)[0] = (uint8_t)( v        & 0xFF);
    (*p)[1] = (uint8_t)((v >>  8) & 0xFF);
    (*p)[2] = (uint8_t)((v >> 16) & 0xFF);
    (*p)[3] = (uint8_t)((v >> 24) & 0xFF);
    *p += 4;
}

static void _wcpy(uint8_t** p, const void* src, size_t n) {
    memcpy(*p, src, n);
    *p += n;
}

static size_t _propsSize(sLayerProps* lp) {
    uint32_t nameLen = (uint32_t)strnlen(lp->layerName, sizeof(lp->layerName));
    return sizeof(float) + 1 + 1 + 1 + 1 + 1 + 1 + 1 + sizeof(uint32_t) + nameLen;
}

static void _writeProps(uint8_t** p, sLayerProps* lp) {
    uint32_t nameLen = (uint32_t)strnlen(lp->layerName, sizeof(lp->layerName));
    _wcpy(p, &lp->op, sizeof(float));
    *(*p)++ = lp->visible ? 1 : 0;
    *(*p)++ = (uint8_t)lp->blendmode;
    *(*p)++ = lp->presop;
    *(*p)++ = lp->droppedup ? 1 : 0;
    *(*p)++ = lp->droppeddown ? 1 : 0;
    *(*p)++ = lp->locked ? 1 : 0;
    *(*p)++ = lp->realidx;
    _wu32(p, nameLen);
    if (nameLen > 0) _wcpy(p, lp->layerName, nameLen);
}

static void _readProps(const uint8_t** p, sLayerProps* lp) {
    memcpy(&lp->op, *p, sizeof(float)); *p += sizeof(float);
    lp->visible    = *(*p)++ != 0;
    lp->blendmode  = *(*p)++;
    lp->presop     = *(*p)++;
    lp->droppedup  = *(*p)++ != 0;
    lp->droppeddown = *(*p)++ != 0;
    lp->locked     = *(*p)++ != 0;
    lp->realidx    = *(*p)++;
    uint32_t nameLen = 0;
    memcpy(&nameLen, *p, 4); *p += 4;
    if (nameLen >= sizeof(lp->layerName)) nameLen = (uint32_t)(sizeof(lp->layerName) - 1);
    if (nameLen > 0) { memcpy(lp->layerName, *p, nameLen); *p += nameLen; }
    lp->layerName[nameLen] = '\0';
}

/* ── Save ──────────────────────────────────────────────────────────────── */

bool SaveRePaint(const char* path, Canvas* canvas, AppState* state) {
    if (!path || !canvas || canvas->layerCount < 1) return false;

    /* 1. GPU composite + dither → 8-bit preview */
    Image flat = CompositeLayersWithDither(state);
    int compSize = 0;
    unsigned char* compPng = ExportImageToMemory(flat, ".png", &compSize);
    UnloadImage(flat);
    if (!compPng || compSize <= 0) return false;

    /* 2. Serialize each layer */
    struct _layerBlob {
        size_t    propsSz;
        uint8_t*  propsData;
        int       pngSz;
        unsigned char* pngData;
    };
    int lc = canvas->layerCount;
    struct _layerBlob* blobs = (struct _layerBlob*)calloc(lc, sizeof(struct _layerBlob));
    if (!blobs) { MemFree(compPng); return false; }

    size_t totalExtra = 0;
    for (int i = 0; i < lc; i++) {
        blobs[i].propsSz = _propsSize(&canvas->layerProps[i]);
        blobs[i].propsData = (uint8_t*)malloc(blobs[i].propsSz);
        if (!blobs[i].propsData) { /* cleanup */ for (int j = 0; j < i; j++) { free(blobs[j].propsData); if (blobs[j].pngData) MemFree(blobs[j].pngData); } free(blobs); MemFree(compPng); return false; }
        uint8_t* wp = blobs[i].propsData;
        _writeProps(&wp, &canvas->layerProps[i]);

        blobs[i].pngData = ExportImageToMemory(canvas->layerImages[i], ".png", &blobs[i].pngSz);
        if (!blobs[i].pngData) {
            for (int j = 0; j <= i; j++) { free(blobs[j].propsData); if (blobs[j].pngData) MemFree(blobs[j].pngData); }
            free(blobs); MemFree(compPng); return false;
        }
        totalExtra += blobs[i].propsSz + blobs[i].pngSz + 4 + 4;
    }

    /* 3. Allocate final buffer */
    size_t hdrSz = MAGIC_LEN + 4 + 4 + 4 + 4;
    size_t totalSz = compSize + hdrSz + totalExtra;
    uint8_t* buf = (uint8_t*)malloc(totalSz);
    if (!buf) {
        for (int i = 0; i < lc; i++) { free(blobs[i].propsData); MemFree(blobs[i].pngData); }
        free(blobs); MemFree(compPng); return false;
    }

    uint8_t* p = buf;
    _wcpy(&p, compPng, compSize);
    _wcpy(&p, MAGIC, MAGIC_LEN);
    _wu32(&p, FILE_VER);
    _wu32(&p, (uint32_t)canvas->width);
    _wu32(&p, (uint32_t)canvas->height);
    _wu32(&p, (uint32_t)lc);

    for (int i = 0; i < lc; i++) {
        _wu32(&p, (uint32_t)blobs[i].propsSz);
        _wcpy(&p, blobs[i].propsData, blobs[i].propsSz);
        _wu32(&p, (uint32_t)blobs[i].pngSz);
        _wcpy(&p, blobs[i].pngData, blobs[i].pngSz);
    }

    bool ok = SaveFileData(path, buf, (int)totalSz);

    for (int i = 0; i < lc; i++) { free(blobs[i].propsData); MemFree(blobs[i].pngData); }
    free(blobs);
    MemFree(compPng);
    free(buf);

    return ok;
}

/* ── Read helpers ──────────────────────────────────────────────────────── */

static uint32_t _ru32(const uint8_t** p) {
    uint32_t v = (*p)[0] | ((uint32_t)(*p)[1] << 8) | ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return v;
}

/* ── Load ──────────────────────────────────────────────────────────────── */

bool LoadRePaint(const char* path, Canvas* canvas) {
    if (!path || !canvas) return false;

    int fileSz = 0;
    unsigned char* fileData = LoadFileData(path, &fileSz);
    if (!fileData || fileSz < 32) { UnloadFileData(fileData); return false; }

    const uint8_t iendSig[8] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D' };
    int offset = 0;
    int found = 0;
    for (; offset < fileSz - 8; offset++) {
        if (memcmp(fileData + offset, iendSig, 8) == 0) { found = 1; break; }
    }
    if (!found) { UnloadFileData(fileData); return false; }
    offset += 12;
    if (offset + (int)MAGIC_LEN + 16 > fileSz) { UnloadFileData(fileData); return false; }

    const uint8_t* p = fileData + offset;
    if (memcmp(p, MAGIC, MAGIC_LEN) != 0) { UnloadFileData(fileData); return false; }
    p += MAGIC_LEN;

    uint32_t ver = _ru32(&p); (void)ver;
    uint32_t w = _ru32(&p);
    uint32_t h = _ru32(&p);
    if (w < 1 || w > 32768 || h < 1 || h > 32768) { UnloadFileData(fileData); return false; }

    uint32_t lc = _ru32(&p);
    if (lc < 1 || lc > 256) { UnloadFileData(fileData); return false; }

    Canvas_Destroy(canvas);
    canvas->width = (int)w;
    canvas->height = (int)h;
    canvas->backgroundColor = WHITE;
    canvas->layerCount = 0;
    canvas->layerImages = NULL;
    canvas->layerProps = NULL;

    for (uint32_t i = 0; i < lc; i++) {
        if ((int)(p - fileData) + 8 > fileSz) { Canvas_Destroy(canvas); UnloadFileData(fileData); return false; }
        uint32_t propSz = _ru32(&p);
        if ((int)(p - fileData) + (int)propSz > fileSz) { Canvas_Destroy(canvas); UnloadFileData(fileData); return false; }
        Canvas_AddLayer(canvas);
        int layerIdx = canvas->layerCount - 1;
        const uint8_t* propStart = p;
        _readProps(&p, &canvas->layerProps[layerIdx]);
        p = propStart + propSz;

        uint32_t pngSz = _ru32(&p);
        if ((int)(p - fileData) + (int)pngSz > fileSz) { Canvas_Destroy(canvas); UnloadFileData(fileData); return false; }
        Image layerImg = LoadImageFromMemory(".png", p, (int)pngSz);
        p += pngSz;
        if (layerImg.data == NULL) { Canvas_Destroy(canvas); UnloadFileData(fileData); return false; }
        UnloadImage(canvas->layerImages[layerIdx]);
        canvas->layerImages[layerIdx] = layerImg;
        if (layerImg.width != (int)w || layerImg.height != (int)h)
            ImageResize(&canvas->layerImages[layerIdx], (int)w, (int)h);
    }

    UnloadFileData(fileData);
    return true;
}
