#pragma once
#include "repaint.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BRUSH_PRESET_NAME_MAX 64
#define BRUSH_PRESET_MAX 256
#define PRESET_FILE_MAGIC "REPRESET"
#define PRESET_FILE_VER 5

typedef struct {
    char name[BRUSH_PRESET_NAME_MAX];

    /* Tool */
    int mode, eraseMode;

    /* BParam slider values (clipmaxF, clipminF, jitter) and pen modes */
    struct { float val, clipmin, jitter; int penMode; } bp[20];

    /* Texture options */
    int texBlendMode, texNoisemode, texColorMode;
    bool useTexLumAsAlpha, texUseRGB;
    char texName[64];

    /* Brush */
    int bmidx;
    bool preserveop, seamlessPaint;
    int strokeSmoothingMode;
    float strokeThrottle;
} BrushPreset;

/* ── File I/O ── */
int  Preset_LoadDefault(BrushPreset* out, int maxCount);
int  Preset_LoadUser(BrushPreset* out, int maxCount);
bool Preset_SaveUser(const BrushPreset* presets, int count);

/* ── Apply / capture helpers ── */
void Preset_CaptureFromCurrent(BrushPreset* p, AppState* state);
void Preset_ApplyToCurrent(const BrushPreset* p, AppState* state);

/* Load and apply preset named 'default' from user file, then bundled */
void Preset_ApplyDefault(AppState* state);
