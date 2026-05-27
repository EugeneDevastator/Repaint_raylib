#include "brush_preset.h"

// ── BParam index mapping ──────────────────────────────────────────────
enum {
    BI_SIZE, BI_SIZEMUL, BI_HARDNESS, BI_CURVATURE, BI_SPACING,
    BI_OPACITY, BI_ANGLE, BI_SCALEREL, BI_CLONEOPACITY, BI_SCATTER,
    BI_POWER, BI_PERSPECTIVE,
    BI_QUICKHUE, BI_QUICKSAT, BI_QUICKLIT,
    BI_TEXSCALE, BI_TEXFEATHER, BI_TEXTHRESH, BI_TEXBLENDVAL,
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
    extern BParam bpAngle, bpScaleRel, bpSizeMul, bpPower, bpPerspective;
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
}

void Preset_CaptureFromCurrent(BrushPreset* p, AppState* state) {
    memset(p, 0, sizeof(*p));
    _ensureBPs();

    p->mode = state->mode;
    p->eraseMode = state->eraseMode;

    for (int i = 0; i < BI_COUNT; i++) {
        p->bp[i].val = _bps[i]->user.clipmaxF;
        p->bp[i].penMode = _bps[i]->penMode;
    }

    p->texBlendMode    = state->currentBrush.Realb.texBlendMode;
    p->texNoisemode    = state->currentBrush.Realb.texNoisemode;
    p->texColorMode    = state->currentBrush.Realb.texColorMode;
    p->useTexLumAsAlpha = state->currentBrush.Realb.useTexLumAsAlpha;
    p->texUseRGB       = state->currentBrush.Realb.texUseRGB;

    if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
        snprintf(p->texName, sizeof(p->texName), "%s", state->brushTex[state->activeBrushTex].name);
    else
        p->texName[0] = '\0';

    p->bmidx = state->currentBrush.Realb.bmidx;
    p->preserveop = state->currentBrush.Realb.preserveop != 0;

    extern bool g_seamlessPaint;
    p->seamlessPaint = g_seamlessPaint;

    extern int g_strokeSmoothingMode;
    extern float g_splineMinDist, g_splineAngleThreshold;
    p->strokeSmoothingMode = g_strokeSmoothingMode;
    p->splineMinDist = g_splineMinDist;
    p->splineAngleThreshold = g_splineAngleThreshold;
}

void Preset_ApplyToCurrent(const BrushPreset* p, AppState* state) {
    _ensureBPs();

    state->mode = p->mode;
    state->eraseMode = p->eraseMode;

    for (int i = 0; i < BI_COUNT; i++) {
        if (i == BI_QUICKHUE || i == BI_QUICKSAT || i == BI_QUICKLIT) continue;
        _bps[i]->user.clipmaxF = p->bp[i].val;
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
        for (int ti = 0; ti < state->brushTexCount; ti++) {
            if (strcmp(state->brushTex[ti].name, p->texName) == 0) {
                state->activeBrushTex = ti;
                break;
            }
        }
    } else {
        state->activeBrushTex = -1;
    }

    extern bool g_seamlessPaint;
    g_seamlessPaint = p->seamlessPaint;

    extern int g_strokeSmoothingMode;
    extern float g_splineMinDist, g_splineAngleThreshold;
    g_strokeSmoothingMode = p->strokeSmoothingMode;
    g_splineMinDist = p->splineMinDist;
    g_splineAngleThreshold = p->splineAngleThreshold;
}

// ── File I/O ──

static int _loadFile(const char* path, BrushPreset* out, int maxCount) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, PRESET_FILE_MAGIC, 8) != 0) {
        fclose(f); return 0;
    }

    unsigned char verBuf[4];
    if (fread(verBuf, 1, 4, f) != 4) { fclose(f); return 0; }
    // version unused for now

    unsigned char countBuf[4];
    if (fread(countBuf, 1, 4, f) != 4) { fclose(f); return 0; }
    int count = countBuf[0] | (countBuf[1] << 8) | (countBuf[2] << 16) | (countBuf[3] << 24);
    if (count < 0 || count > maxCount) { fclose(f); return 0; }

    size_t readSz = fread(out, sizeof(BrushPreset), count, f);
    fclose(f);
    return (int)readSz;
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
    if (n == 0) return Preset_LoadDefault(out, maxCount); // fall back to defaults
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
    // Not found in user — try bundled
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
