#include "file_format.h"
#include "repaint.h"
#include <stdlib.h>
#include <string.h>

#define MAGIC      "REPAINT"
#define MAGIC_LEN  8
#define FILE_VER   5

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
    return sizeof(float)       // op
         + 1                   // visible
         + 1                   // blendmode
         + 1                   // presop
         + 1 + 1 + 1 + 1       // droppedup/down, locked, realidx
         + sizeof(uint32_t) + nameLen  // layerName
         + 6 * sizeof(float)   // mat[6]
         + sizeof(int) * 2     // layerW, layerH
         + sizeof(float) * 2;  // threshold, feather (v5 — was missing!)
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
    _wcpy(p, lp->mat, 6 * sizeof(float));               // v4+
    _wu32(p, (uint32_t)lp->layerW);                     // v5+: native width
    _wu32(p, (uint32_t)lp->layerH);                     // v5+: native height
    _wcpy(p, &lp->threshold, sizeof(float));             // v5+: was missing before
    _wcpy(p, &lp->feather,  sizeof(float));             // v5+: was missing before
}

static uint32_t _ru32(const uint8_t** p); // forward decl

static void _readProps(const uint8_t** p, sLayerProps* lp, uint32_t ver) {
    memcpy(&lp->op, *p, sizeof(float)); *p += sizeof(float);
    lp->visible     = *(*p)++ != 0;
    lp->blendmode   = *(*p)++;
    lp->presop      = *(*p)++;
    lp->droppedup   = *(*p)++ != 0;
    lp->droppeddown = *(*p)++ != 0;
    lp->locked      = *(*p)++ != 0;
    lp->realidx     = *(*p)++;
    uint32_t nameLen = 0;
    memcpy(&nameLen, *p, 4); *p += 4;
    if (nameLen >= sizeof(lp->layerName)) nameLen = (uint32_t)(sizeof(lp->layerName) - 1);
    if (nameLen > 0) { memcpy(lp->layerName, *p, nameLen); *p += nameLen; }
    lp->layerName[nameLen] = '\0';
    // mat[6] — format v4+
    if (ver >= 4) { memcpy(lp->mat, *p, 6 * sizeof(float)); *p += 6 * sizeof(float); }
    else { lp->mat[0] = 1; lp->mat[1] = 0; lp->mat[2] = 0; lp->mat[3] = 0; lp->mat[4] = 1; lp->mat[5] = 0; }
    // layerW, layerH — format v5+
    if (ver >= 5) {
        lp->layerW = (int)_ru32(p); lp->layerH = (int)_ru32(p);
        memcpy(&lp->threshold, *p, sizeof(float)); *p += sizeof(float);
        memcpy(&lp->feather,  *p, sizeof(float)); *p += sizeof(float);
    } else {
        lp->layerW = lp->layerH = 0;
    }
}

/* ── Save ──────────────────────────────────────────────────────────────── */

bool SaveRePaint(const char* path, Document* doc, AppState* state) {
    if (!path || !doc || LayerStack_Count() < 1) return false;

    // 1. GPU composite + dithered 8-bit preview (for file thumbnails)
    Image flat = CompositeLayersWithDither(state);
    int compSize = 0;
    unsigned char* compPng = ExportImageToMemory(flat, ".png", &compSize);
    UnloadImage(flat);
    if (!compPng || compSize <= 0) return false;

    // 2. Serialize each layer's props (pixel data written later from the live image)
    int lc = LayerStack_Count();
    size_t totalExtra = 0;
    uint32_t pixelDepth = 1;  // 1 = raw R16G16B16A16

    struct { size_t propsSz; uint8_t* propsData; } blobs[256];
    for (int i = 0; i < lc; i++) {
        sLayerProps* p = LayerStack_GetProps(i);
        blobs[i].propsSz = _propsSize(p);
        blobs[i].propsData = (uint8_t*)malloc(blobs[i].propsSz);
        if (!blobs[i].propsData) { for (int j = 0; j < i; j++) free(blobs[j].propsData); MemFree(compPng); return false; }
        uint8_t* wp = blobs[i].propsData;
        _writeProps(&wp, p);

        int rawSz = p->layerW * p->layerH * 8;
        totalExtra += 4 + blobs[i].propsSz + 4 + rawSz;
    }

    // 3. Serialize user textures (skip built-in defaults)
    // Collect non-built-in textures from TM bucket 0
    struct { TexSlotID id; uint8_t* png; int pngSz; } texBlobs[256];
    int tc = 0;
    for (int s = 0; s < TM_SLOTS_PER_BUCKET && tc < 256; s++) {
        TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
        TexSlot* ts = TM_Get(id);
        if (!ts || ts->builtIn) continue;
        Image texPngImg = ts->cpuImage;
        bool owned = false;
        if (texPngImg.data && texPngImg.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
            texPngImg = ImageCopy(texPngImg);
            ImageFormat(&texPngImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            owned = true;
        }
        int pngSz = 0;
        uint8_t* png = ExportImageToMemory(texPngImg, ".png", &pngSz);
        if (owned) UnloadImage(texPngImg);
        texBlobs[tc].id = id;
        texBlobs[tc].png = png;
        texBlobs[tc].pngSz = pngSz;
        tc++;
    }

    // Compute total texture section size
    size_t texTotalSz = 0;
    for (int i = 0; i < tc; i++) {
        if (texBlobs[i].png) texTotalSz += 4 + 4 + 4 + 4 + 64 + texBlobs[i].pngSz;
    }

    // 4. Write final buffer
    size_t hdrSz = MAGIC_LEN + 4 + 4 + 4 + 4 + 4; // +4 for pixelDepth
    size_t totalSz = compSize + hdrSz + totalExtra + texTotalSz + 4;
    uint8_t* buf = (uint8_t*)malloc(totalSz);
    if (!buf) {
        for (int i = 0; i < lc; i++) free(blobs[i].propsData);
        MemFree(compPng);
        for (int i = 0; i < tc; i++) if (texBlobs[i].png) MemFree(texBlobs[i].png);
        return false;
    }

    uint8_t* p = buf;
    _wcpy(&p, compPng, compSize);
    _wcpy(&p, MAGIC, MAGIC_LEN);
    _wu32(&p, FILE_VER);
    _wu32(&p, (uint32_t)doc->width);
    _wu32(&p, (uint32_t)doc->height);
    _wu32(&p, (uint32_t)lc);
    _wu32(&p, pixelDepth);

    for (int i = 0; i < lc; i++) {
        sLayerProps* props = LayerStack_GetProps(i);
        Image* img = LayerStack_GetImage(i);
        _wu32(&p, (uint32_t)blobs[i].propsSz);
        _wcpy(&p, blobs[i].propsData, blobs[i].propsSz);
        // pixelDepth == 1: write raw R16G16B16A16 pixel data (8 bytes/px)
        int rawSz = props->layerW * props->layerH * 8;
        _wu32(&p, (uint32_t)rawSz);
        _wcpy(&p, img->data, rawSz);
    }

    /* User texture section (built-in defaults not saved) */
    _wu32(&p, (uint32_t)tc);
    for (int i = 0; i < tc; i++) {
        TexSlot* ts = TM_Get(texBlobs[i].id);
        if (!ts) continue;
        uint32_t nlen = (uint32_t)strnlen(ts->name, 64);
        _wu32(&p, nlen);
        _wcpy(&p, ts->name, nlen);
        _wu32(&p, (uint32_t)ts->w);
        _wu32(&p, (uint32_t)ts->h);
        _wu32(&p, (uint32_t)texBlobs[i].pngSz);
        if (texBlobs[i].pngSz > 0) _wcpy(&p, texBlobs[i].png, texBlobs[i].pngSz);
    }

    bool ok = SaveFileData(path, buf, (int)totalSz);

    for (int i = 0; i < lc; i++) free(blobs[i].propsData);
    MemFree(compPng);
    for (int i = 0; i < tc; i++) if (texBlobs[i].png) MemFree(texBlobs[i].png);
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

bool LoadRePaint(const char* path, Document* doc, AppState* state) {
    if (!path || !doc) return false;

    int fileSz = 0;
    unsigned char* fileData = LoadFileData(path, &fileSz);
    if (!fileData || fileSz < 32) { UnloadFileData(fileData); return false; }

    const uint8_t iendSig[8] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D' };
    int offset = 0; int found = 0;
    for (; offset < fileSz - 8; offset++) { if (memcmp(fileData + offset, iendSig, 8) == 0) { found = 1; break; } }
    if (!found) { UnloadFileData(fileData); return false; }
    offset += 12;
    if (offset + (int)MAGIC_LEN + 16 > fileSz) { UnloadFileData(fileData); return false; }

    const uint8_t* p = fileData + offset;
    if (memcmp(p, MAGIC, MAGIC_LEN) != 0) { UnloadFileData(fileData); return false; }
    p += MAGIC_LEN;

    uint32_t ver = _ru32(&p);
    uint32_t w = _ru32(&p), h = _ru32(&p);
    if (w < 1 || w > 32768 || h < 1 || h > 32768) { UnloadFileData(fileData); return false; }
    uint32_t lc = _ru32(&p);
    if (lc < 1 || lc > 256) { UnloadFileData(fileData); return false; }

    uint32_t pixelDepth = 0;
    if (ver >= 5) pixelDepth = _ru32(&p);

    doc->width = (int)w; doc->height = (int)h;

    if (ver < 5) {
        // v3/v4: skip per-layer blobs, load composite preview as single layer
        for (uint32_t i = 0; i < lc; i++) {
            if ((int)(p - fileData) + 8 > fileSz) { UnloadFileData(fileData); return false; }
            uint32_t propSz = _ru32(&p); if ((int)(p - fileData) + (int)propSz > fileSz) { UnloadFileData(fileData); return false; }
            p += propSz;
            uint32_t pngSz = _ru32(&p); if ((int)(p - fileData) + (int)pngSz > fileSz) { UnloadFileData(fileData); return false; }
            p += pngSz;
        }
        Image preview = LoadImageFromMemory(".png", fileData, (int)(offset + 12));
        if (preview.data) {
            if (preview.width != (int)w || preview.height != (int)h) ImageResize(&preview, (int)w, (int)h);
            int pxCount = preview.width * preview.height;
            uint16_t* d16 = (uint16_t*)malloc(pxCount * 4 * sizeof(uint16_t));
            uint8_t* s8 = (uint8_t*)preview.data;
            for (int pi = 0; pi < pxCount; pi++) {
                d16[pi*4] = (uint16_t)s8[pi*4]*257; d16[pi*4+1] = (uint16_t)s8[pi*4+1]*257;
                d16[pi*4+2] = (uint16_t)s8[pi*4+2]*257; d16[pi*4+3] = (uint16_t)s8[pi*4+3]*257;
            }
            free(preview.data); preview.data = d16; preview.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
            int idx = LayerStack_Add((int)w, (int)h);
            UnloadImage(*LayerStack_GetImage(idx));
            *LayerStack_GetImage(idx) = preview;
            sLayerProps* lp = LayerStack_GetProps(idx);
            lp->op = 1; lp->visible = true; lp->blendmode = bmGamma;
            lp->mat[0] = 1; lp->mat[4] = 1; lp->layerW = (int)w; lp->layerH = (int)h;
            LayerStack_SyncRTFromImage(idx);
        }
    } else {
        // v5+: load each layer at its native resolution
        for (uint32_t i = 0; i < lc; i++) {
            if ((int)(p - fileData) + 8 > fileSz) { UnloadFileData(fileData); return false; }
            uint32_t propSz = _ru32(&p);
            if ((int)(p - fileData) + (int)propSz > fileSz) { UnloadFileData(fileData); return false; }
            sLayerProps tempProps; memset(&tempProps, 0, sizeof(tempProps));
            const uint8_t* propStart = p;
            _readProps(&p, &tempProps, ver);
            p = propStart + propSz;
            int lw = tempProps.layerW > 0 ? tempProps.layerW : (int)w;
            int lh = tempProps.layerH > 0 ? tempProps.layerH : (int)h;

            uint32_t dataSz = _ru32(&p);
            if ((int)(p - fileData) + (int)dataSz > fileSz) { UnloadFileData(fileData); return false; }

            Image layerImg; memset(&layerImg, 0, sizeof(layerImg));
            if (pixelDepth == 1) {
                layerImg.width = lw; layerImg.height = lh;
                layerImg.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16; layerImg.mipmaps = 1;
                layerImg.data = malloc(dataSz);
                if (!layerImg.data) { UnloadFileData(fileData); return false; }
                memcpy(layerImg.data, p, dataSz); p += dataSz;
            } else {
                layerImg = LoadImageFromMemory(".png", p, (int)dataSz); p += dataSz;
                if (layerImg.data == NULL) { UnloadFileData(fileData); return false; }
                if (layerImg.width != lw || layerImg.height != lh) ImageResize(&layerImg, lw, lh);
                int pxCount = lw * lh;
                uint16_t* d16 = (uint16_t*)malloc(pxCount * 4 * sizeof(uint16_t));
                uint8_t* s8 = (uint8_t*)layerImg.data;
                for (int pi = 0; pi < pxCount; pi++) {
                    d16[pi*4] = (uint16_t)s8[pi*4]*257; d16[pi*4+1] = (uint16_t)s8[pi*4+1]*257;
                    d16[pi*4+2] = (uint16_t)s8[pi*4+2]*257; d16[pi*4+3] = (uint16_t)s8[pi*4+3]*257;
                }
                free(layerImg.data); layerImg.data = d16; layerImg.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
            }

            int idx = LayerStack_Add(lw, lh);
            *LayerStack_GetImage(idx) = layerImg;
            *LayerStack_GetProps(idx) = tempProps;
            LayerStack_GetProps(idx)->layerW = lw; LayerStack_GetProps(idx)->layerH = lh;
            // Upload the loaded CPU image to the GPU render target
            LayerStack_SyncRTFromImage(idx);
        }
    }

    // Load textures (v2+)
    if (ver >= 2 && state != NULL) {
        // Remove existing non-built-in user textures
        TexSlotID toRemove[256];
        int removeCount = 0;
        for (int s = 0; s < TM_SLOTS_PER_BUCKET && removeCount < 256; s++) {
            TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
            TexSlot* ts = TM_Get(id);
            if (ts && !ts->builtIn) toRemove[removeCount++] = id;
        }
        for (int i = 0; i < removeCount; i++) TM_Remove(toRemove[i]);

        state->brushTexSlot = TM_INVALID_SLOT;
        state->editTexSlot = TM_INVALID_SLOT;
        state->editTexMode = 0;
        state->brushTexActive = false;

        uint32_t tc = _ru32(&p);
        for (uint32_t ti = 0; ti < tc; ti++) {
            uint32_t nlen = _ru32(&p);
            if (nlen > 63) nlen = 63;
            char name[64] = {0};
            if (nlen > 0) { memcpy(name, p, nlen); p += nlen; }
            uint32_t tw = _ru32(&p);
            uint32_t th = _ru32(&p);
            uint32_t tsz = _ru32(&p);
            TexSlotID id = BrushTex_Add(state, name, (int)tw, (int)th);
            if (TM_IsValid(id) && tsz > 0 && (int)(p - fileData) + (int)tsz <= fileSz) {
                Image timg = LoadImageFromMemory(".png", p, (int)tsz);
                p += tsz;
                if (timg.data) {
                    ImageFormat(&timg, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
                    TexSlot* ts = TM_Get(id);
                    if (ts) {
                        UnloadImage(ts->cpuImage);
                        ts->cpuImage = timg;
                        ts->dirty = true;
                    }
                }
            } else {
                p += tsz;
            }
        }
        BrushTex_SyncAll(state);
    }

    UnloadFileData(fileData);
    return true;
}
