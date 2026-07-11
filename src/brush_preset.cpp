#include "brush_preset.h"

// ── BParam index mapping ──────────────────────────────────────────────
enum {
    BI_SIZE, BI_SIZEMUL, BI_HARDNESS, BI_CURVATURE, BI_SPACING,
    BI_OPACITY, BI_ANGLE, BI_SCALEREL, BI_CLONEOPACITY, BI_SCATTER,
    BI_POWER, BI_PERSPECTIVE,
    BI_QUICKHUE, BI_QUICKSAT, BI_QUICKLIT,
    BI_TEXSCALE, BI_TEXFEATHER, BI_TEXTHRESH, BI_TEXBLENDVAL,
    BI_FOCALOFFSET,
    BI_COUNT
};

static BParam* _bps[BI_COUNT];
static bool _bpsInited = false;

static void _ensureBPs(void) {
    if (_bpsInited) return;
    _bpsInited = true;
    extern BParam bpOpacity, bpSize, bpHardness, bpSpacing, bpCurvature, bpScatter;
    extern BParam bpCloneOpacity, bpQuickHue, bpQuickSat, bpQuickLit;
    extern BParam bpTexScale, bpTexFeather, bpTexThresh, bpTexBlendVal;
    extern BParam bpAngle, bpScaleRel, bpSizeMul, bpPower, bpPerspective, bpFocalOffset;
    _bps[BI_SIZE]        = &bpSize;
    _bps[BI_SIZEMUL]     = &bpSizeMul;
    _bps[BI_HARDNESS]    = &bpHardness;
    _bps[BI_CURVATURE]   = &bpCurvature;
    _bps[BI_SPACING]     = &bpSpacing;
    _bps[BI_OPACITY]     = &bpOpacity;
    _bps[BI_ANGLE]       = &bpAngle;
    _bps[BI_SCALEREL]    = &bpScaleRel;
    _bps[BI_CLONEOPACITY]= &bpCloneOpacity;
    _bps[BI_SCATTER]     = &bpScatter;
    _bps[BI_POWER]       = &bpPower;
    _bps[BI_PERSPECTIVE] = &bpPerspective;
    _bps[BI_QUICKHUE]    = &bpQuickHue;
    _bps[BI_QUICKSAT]    = &bpQuickSat;
    _bps[BI_QUICKLIT]    = &bpQuickLit;
    _bps[BI_TEXSCALE]    = &bpTexScale;
    _bps[BI_TEXFEATHER]  = &bpTexFeather;
    _bps[BI_TEXTHRESH]   = &bpTexThresh;
    _bps[BI_TEXBLENDVAL] = &bpTexBlendVal;
    _bps[BI_FOCALOFFSET] = &bpFocalOffset;
}

void Preset_CaptureFromCurrent(BrushPreset* p, AppState* state) {
    memset(p, 0, sizeof(*p));
    _ensureBPs();

    p->mode = state->mode;
    p->eraseMode = state->eraseMode;

    for (int i = 0; i < BI_COUNT; i++) {
        p->bp[i].val = _bps[i]->user.clipmaxF;
        p->bp[i].clipmin = _bps[i]->user.clipminF;
        p->bp[i].jitter = _bps[i]->user.jitter;
        p->bp[i].penMode = _bps[i]->penMode;
    }

    p->texBlendMode    = state->currentBrush.Realb.texBlendMode;
    p->texNoisemode    = state->currentBrush.Realb.texNoisemode;
    p->texColorMode    = state->currentBrush.Realb.texColorMode;
    p->useTexLumAsAlpha = state->currentBrush.Realb.useTexLumAsAlpha;
    p->texUseRGB       = state->currentBrush.Realb.texUseRGB;

    TexSlot* ts = TM_Get(state->brushTexSlot);
    if (ts)
        snprintf(p->texName, sizeof(p->texName), "%s", ts->name);
    else
        p->texName[0] = '\0';

    p->bmidx = state->currentBrush.Realb.bmidx;
    p->preserveop = state->currentBrush.Realb.preserveop != 0;

    extern bool g_seamlessPaint;
    p->seamlessPaint = g_seamlessPaint;

    extern int g_strokeSmoothingMode;
    extern float g_strokeThrottle;
    p->strokeSmoothingMode = g_strokeSmoothingMode;
    p->strokeThrottle = g_strokeThrottle;
}

// ── Validation ────────────────────────────────────────────────────────
static bool _presetIsValid(const BrushPreset* p) {
    if (!p->name[0]) return false;
    for (int i = 0; i < BRUSH_PRESET_NAME_MAX; i++) {
        if (p->name[i] == '\0') break;
        if (i == BRUSH_PRESET_NAME_MAX - 1) return false;
    }
    if (p->mode < 0 || p->mode > eSingleStamp) return false;
    if (p->eraseMode < 0 || p->eraseMode > 2) return false;
    for (int i = 0; i < 64; i++) {
        if (p->texName[i] == '\0') break;
        if (i == 63) return false;
    }
    return true;
}

void Preset_ApplyToCurrent(const BrushPreset* p, AppState* state) {
    if (!_presetIsValid(p)) return;
    _ensureBPs();

    state->mode = p->mode;
    state->eraseMode = p->eraseMode;

    for (int i = 0; i < BI_COUNT; i++) {
        _bps[i]->user.clipmaxF = p->bp[i].val;
        _bps[i]->user.clipminF = p->bp[i].clipmin;
        _bps[i]->user.jitter = p->bp[i].jitter;
        _bps[i]->penMode = p->bp[i].penMode;
    }

    state->currentBrush.Realb.texBlendMode    = p->texBlendMode;
    state->currentBrush.Realb.texNoisemode    = p->texNoisemode;
    state->currentBrush.Realb.texColorMode    = p->texColorMode;
    state->currentBrush.Realb.useTexLumAsAlpha = p->useTexLumAsAlpha;
    state->currentBrush.Realb.texUseRGB       = p->texUseRGB;
    state->currentBrush.Realb.bmidx           = p->bmidx;
    state->currentBrush.Realb.preserveop      = p->preserveop ? 1 : 0;

    if (p->texName[0]) {
        bool found = false;
        for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++) {
            TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
            TexSlot* ts = TM_Get(id);
            if (ts && strcmp(ts->name, p->texName) == 0) {
                state->brushTexSlot = id;
                found = true;
                break;
            }
        }
        if (!found) state->brushTexSlot = TM_INVALID_SLOT;
    } else {
        state->brushTexSlot = TM_INVALID_SLOT;
    }

    extern bool g_seamlessPaint;
    g_seamlessPaint = p->seamlessPaint;

    extern int g_strokeSmoothingMode;
    extern float g_strokeThrottle;
    g_strokeSmoothingMode = p->strokeSmoothingMode;
    g_strokeThrottle = p->strokeThrottle;
}

// ── File I/O ──

// Version-1: splineMinDist + splineAngleThreshold, old bp[19]{float,int}, no focalOffset
struct PresetV1 {
    char name[64];
    int mode, eraseMode;
    struct { float val; int penMode; } bp[19];
    int texBlendMode, texNoisemode, texColorMode;
    bool useTexLumAsAlpha, texUseRGB;
    char texName[64];
    int bmidx;
    bool preserveop, seamlessPaint;
    int strokeSmoothingMode;
    float splineMinDist;
    float splineAngleThreshold;
};
static_assert(sizeof(PresetV1) == 324, "PresetV1 layout mismatch");

// Version-2: strokeThrottle, no splineAngle, old bp[19]{float,int}, no focalOffset
struct PresetV2 {
    char name[64];
    int mode, eraseMode;
    struct { float val; int penMode; } bp[19];
    int texBlendMode, texNoisemode, texColorMode;
    bool useTexLumAsAlpha, texUseRGB;
    char texName[64];
    int bmidx;
    bool preserveop, seamlessPaint;
    int strokeSmoothingMode;
    float strokeThrottle;
};
static_assert(sizeof(PresetV2) == 320, "PresetV2 layout mismatch");

// Version-3: old bp[19]{float,int} + focalOffsetVal/penMode at end
struct PresetV3 {
    char name[64];
    int mode, eraseMode;
    struct { float val; int penMode; } bp[19];
    int texBlendMode, texNoisemode, texColorMode;
    bool useTexLumAsAlpha, texUseRGB;
    char texName[64];
    int bmidx;
    bool preserveop, seamlessPaint;
    int strokeSmoothingMode;
    float strokeThrottle;
    float focalOffsetVal;
    int focalOffsetPenMode;
};
static_assert(sizeof(PresetV3) == 328, "PresetV3 layout mismatch");

// Version-4: new bp[19]{val,clipmin,jitter,penMode} + focalOffsetVal/penMode at end
struct PresetV4 {
    char name[64];
    int mode, eraseMode;
    struct { float val, clipmin, jitter; int penMode; } bp[19];
    int texBlendMode, texNoisemode, texColorMode;
    bool useTexLumAsAlpha, texUseRGB;
    char texName[64];
    int bmidx;
    bool preserveop, seamlessPaint;
    int strokeSmoothingMode;
    float strokeThrottle;
    float focalOffsetVal;
    int focalOffsetPenMode;
};

static int _loadFile(const char* path, BrushPreset* out, int maxCount) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, PRESET_FILE_MAGIC, 8) != 0) {
        fclose(f); return 0;
    }

    unsigned char verBuf[4];
    if (fread(verBuf, 1, 4, f) != 4) { fclose(f); return 0; }

    unsigned char countBuf[4];
    if (fread(countBuf, 1, 4, f) != 4) { fclose(f); return 0; }
    int count = countBuf[0] | (countBuf[1] << 8) | (countBuf[2] << 16) | (countBuf[3] << 24);
    if (count < 0 || count > maxCount) { fclose(f); return 0; }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 16, SEEK_SET);

    long dataSize = fileSize - 16;
    int entrySize = (count > 0) ? (int)(dataSize / count) : 0;
    if (entrySize <= 0 || dataSize % count != 0) { fclose(f); return 0; }

    int loaded = 0;

    // Seed clipmin/jitter/penMode for all bp entries from live BParams
    // so old-format files that lack those fields keep the user's live values.
    _ensureBPs();
    for (int i = 0; i < count && i < maxCount; i++) {
        BrushPreset* p = &out[i];
        memset(p, 0, sizeof(*p));
        for (int j = 0; j < BI_COUNT; j++) {
            p->bp[j].clipmin = _bps[j]->user.clipminF;
            p->bp[j].jitter  = _bps[j]->user.jitter;
            p->bp[j].penMode = _bps[j]->penMode;
        }
    }

    if (entrySize == sizeof(BrushPreset)) {
        // V5 format — read directly
        size_t readSz = fread(out, sizeof(BrushPreset), count, f);
        loaded = (int)readSz;
    } else if (entrySize == sizeof(PresetV4)) {
        // V4 format — read, collapse separate focalOffset into bp[19]
        for (int i = 0; i < count && i < maxCount; i++) {
            PresetV4 old;
            if (fread(&old, sizeof(PresetV4), 1, f) != 1) break;
            BrushPreset* p = &out[loaded];
            memcpy(p->name, old.name, sizeof(p->name));
            p->mode = old.mode;
            p->eraseMode = old.eraseMode;
            for (int j = 0; j < 19; j++) {
                p->bp[j].val     = old.bp[j].val;
                p->bp[j].clipmin = old.bp[j].clipmin;
                p->bp[j].jitter  = old.bp[j].jitter;
                p->bp[j].penMode = old.bp[j].penMode;
                // (already seeded from live, overwrite val only)
            }
            // Focal offset pre-V5 stored as separate fields — put into bp[19]
            p->bp[19].val     = old.focalOffsetVal;
            p->bp[19].penMode = old.focalOffsetPenMode;
            p->texBlendMode = old.texBlendMode;
            p->texNoisemode = old.texNoisemode;
            p->texColorMode = old.texColorMode;
            p->useTexLumAsAlpha = old.useTexLumAsAlpha;
            p->texUseRGB = old.texUseRGB;
            memcpy(p->texName, old.texName, sizeof(p->texName));
            p->bmidx = old.bmidx;
            p->preserveop = old.preserveop;
            p->seamlessPaint = old.seamlessPaint;
            p->strokeSmoothingMode = old.strokeSmoothingMode;
            p->strokeThrottle = old.strokeThrottle;
            if (_presetIsValid(p))
                loaded++;
        }
    } else if (entrySize == sizeof(PresetV3)) {
        // V3 format — old bp[19]{float,int}, focalOffset as separate fields
        for (int i = 0; i < count && i < maxCount; i++) {
            PresetV3 old;
            if (fread(&old, sizeof(PresetV3), 1, f) != 1) break;
            BrushPreset* p = &out[loaded];
            memcpy(p->name, old.name, sizeof(p->name));
            p->mode = old.mode;
            p->eraseMode = old.eraseMode;
            for (int j = 0; j < 19; j++) {
                p->bp[j].val = old.bp[j].val;
                // clipmin/jitter/penMode kept from seed
            }
            p->bp[19].val     = old.focalOffsetVal;
            p->bp[19].penMode = old.focalOffsetPenMode;
            p->texBlendMode = old.texBlendMode;
            p->texNoisemode = old.texNoisemode;
            p->texColorMode = old.texColorMode;
            p->useTexLumAsAlpha = old.useTexLumAsAlpha;
            p->texUseRGB = old.texUseRGB;
            memcpy(p->texName, old.texName, sizeof(p->texName));
            p->bmidx = old.bmidx;
            p->preserveop = old.preserveop;
            p->seamlessPaint = old.seamlessPaint;
            p->strokeSmoothingMode = old.strokeSmoothingMode;
            p->strokeThrottle = old.strokeThrottle;
            if (_presetIsValid(p))
                loaded++;
        }
    } else if (entrySize == sizeof(PresetV2)) {
        for (int i = 0; i < count && i < maxCount; i++) {
            PresetV2 old;
            if (fread(&old, sizeof(PresetV2), 1, f) != 1) break;
            BrushPreset* p = &out[loaded];
            memcpy(p->name, old.name, sizeof(p->name));
            p->mode = old.mode;
            p->eraseMode = old.eraseMode;
            for (int j = 0; j < 19; j++) {
                p->bp[j].val = old.bp[j].val;
            }
            p->texBlendMode = old.texBlendMode;
            p->texNoisemode = old.texNoisemode;
            p->texColorMode = old.texColorMode;
            p->useTexLumAsAlpha = old.useTexLumAsAlpha;
            p->texUseRGB = old.texUseRGB;
            memcpy(p->texName, old.texName, sizeof(p->texName));
            p->bmidx = old.bmidx;
            p->preserveop = old.preserveop;
            p->seamlessPaint = old.seamlessPaint;
            p->strokeSmoothingMode = old.strokeSmoothingMode;
            p->strokeThrottle = old.strokeThrottle;
            // focalOffset (bp[19]) kept from seed
            if (_presetIsValid(p))
                loaded++;
        }
    } else if (entrySize == sizeof(PresetV1)) {
        for (int i = 0; i < count && i < maxCount; i++) {
            PresetV1 old;
            if (fread(&old, sizeof(PresetV1), 1, f) != 1) break;
            BrushPreset* p = &out[loaded];
            memcpy(p->name, old.name, sizeof(p->name));
            p->mode = old.mode;
            p->eraseMode = old.eraseMode;
            for (int j = 0; j < 19; j++) {
                p->bp[j].val = old.bp[j].val;
            }
            p->texBlendMode = old.texBlendMode;
            p->texNoisemode = old.texNoisemode;
            p->texColorMode = old.texColorMode;
            p->useTexLumAsAlpha = old.useTexLumAsAlpha;
            p->texUseRGB = old.texUseRGB;
            memcpy(p->texName, old.texName, sizeof(p->texName));
            p->bmidx = old.bmidx;
            p->preserveop = old.preserveop;
            p->seamlessPaint = old.seamlessPaint;
            p->strokeSmoothingMode = old.strokeSmoothingMode;
            p->strokeThrottle = old.splineMinDist;
            if (_presetIsValid(p))
                loaded++;
        }
    } else {
        fclose(f); return 0;
    }

    fclose(f);
    return loaded;
}

int Preset_LoadDefault(BrushPreset* out, int maxCount) {
    const char* ad = GetApplicationDirectory();
    char path[1024];
    snprintf(path, sizeof(path), "%sresources/default_presets.dat", ad);
    return _loadFile(path, out, maxCount);
}

int Preset_LoadUser(BrushPreset* out, int maxCount) {
    const char* ad = GetApplicationDirectory();
    char path[1024];
    snprintf(path, sizeof(path), "%sSaves/user_presets.dat", ad);
    int n = _loadFile(path, out, maxCount);
    if (n == 0) return Preset_LoadDefault(out, maxCount);
    return n;
}

void Preset_ApplyDefault(AppState* state) {
    BrushPreset buf[256];
    int n = Preset_LoadUser(buf, 256);
    for (int i = 0; i < n; i++) {
        if (strcasecmp(buf[i].name, "default") == 0) {
            Preset_ApplyToCurrent(&buf[i], state);
            return;
        }
    }
    n = Preset_LoadDefault(buf, 256);
    for (int i = 0; i < n; i++) {
        if (strcasecmp(buf[i].name, "default") == 0) {
            Preset_ApplyToCurrent(&buf[i], state);
            return;
        }
    }
}

bool Preset_SaveUser(const BrushPreset* presets, int count) {
    const char* ad = GetApplicationDirectory();
    char path[1024];
    snprintf(path, sizeof(path), "%sSaves/user_presets.dat", ad);

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    fwrite(PRESET_FILE_MAGIC, 1, 8, f);

    unsigned char verBuf[4] = { PRESET_FILE_VER & 0xFF, (PRESET_FILE_VER >> 8) & 0xFF, 0, 0 };
    fwrite(verBuf, 1, 4, f);

    unsigned char countBuf[4] = {
        (unsigned char)(count & 0xFF),
        (unsigned char)((count >> 8) & 0xFF),
        (unsigned char)((count >> 16) & 0xFF),
        (unsigned char)((count >> 24) & 0xFF)
    };
    fwrite(countBuf, 1, 4, f);

    size_t wrote = fwrite(presets, sizeof(BrushPreset), count, f);
    fclose(f);
    return wrote == (size_t)count;
}
