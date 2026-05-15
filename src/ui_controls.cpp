#include "repaint.h"

static Texture2D penModeTex[PEN_MODE_COUNT];
static const char* PenIconNames[PEN_MODE_COUNT] = {
    "tct_none", "tct_pressure", "tct_vel", "tct_dir", "tct_rot", "tct_tilt",
    "tct_relang", "tct_htilt", "tct_vtilt", "tct_lenpx", "tct_acc",
    "tct_xtilt", "tct_ytilt"
};

const char* PenModeNames[PEN_MODE_COUNT] = {
    "Off", "Pressure", "Velocity", "Direction", "Rotation", "Tilt",
    "Rel Ang", "H-Tilt", "V-Tilt", "Length", "Accel",
    "X-Tilt", "Y-Tilt"
};

void DualSlider_Init(DualSlider* slider) {
    slider->clipminF = 0.0f;
    slider->clipmaxF = 1.0f;
    slider->jitter = 0.0f;
    slider->DsRange = 0.25f;
    slider->ActivePick = -1;
    slider->orient = 0;
    slider->Soff = 2;
    slider->sliderrad = 2;
    slider->gradStart = Color{0, 0, 0, 255};
    slider->gradEnd = Color{255, 255, 255, 255};
    slider->shade = Color{144, 144, 144, 255};
    slider->hlite = Color{250, 250, 250, 255};
    slider->midtone = Color{240, 240, 240, 255};
    slider->showValue = true;
    slider->noGradient = false;
    slider->label[0] = '\0';
    slider->prevDown[0] = false;
    slider->prevDown[1] = false;
    slider->prevDown[2] = false;
}



void LoadPenIcons(void) {
    for (int i = 0; i < PEN_MODE_COUNT; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", PenIconNames[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 24, 24);
            penModeTex[i] = LoadTextureFromImage(img);
            UnloadImage(img);
        } else {
            penModeTex[i] = Texture2D{0};
        }
    }
}

Texture2D GetPenModeIcon(int mode) {
    if (mode >= 0 && mode < PEN_MODE_COUNT)
        return penModeTex[mode];
    return Texture2D{0};
}

void UnloadPenIcons(void) {
    for (int i = 0; i < PEN_MODE_COUNT; i++) {
        if (penModeTex[i].id > 0) UnloadTexture(penModeTex[i]);
    }
}

void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef) {
    DualSlider_Init(&bp->slider);
    bp->defClipmaxF = (outMax > outMin) ? (outDef - outMin) / (outMax - outMin) : 1.0f;
    bp->defClipmaxF = fmaxf(0.0f, fminf(1.0f, bp->defClipmaxF));
    bp->slider.clipmaxF = bp->defClipmaxF;
    bp->slider.showValue = false;
    bp->iconTex = Texture2D{0};
    bp->iconLoaded = false;
    bp->penMode = csNone;
    bp->penEdit = false;
    bp->penPending = false;
    bp->outMin = outMin;
    bp->outMax = outMax;
    bp->id = id;
    strncpy(bp->name, name, sizeof(bp->name) - 1);
    bp->tooltip[0] = '\0';
}

void BParam_SetIcon(BParam* bp, const char* filename) {
    char path[128];
    sprintf(path, "resources/%s.png", filename);
    if (FileExists(path)) {
        Image img = LoadImage(path);
        ImageResize(&img, 24, 24);
        bp->iconTex = LoadTextureFromImage(img);
        bp->iconLoaded = bp->iconTex.id > 0;
        UnloadImage(img);
    } else {
        bp->iconTex = Texture2D{0};   // <-- added
        bp->iconLoaded = false;
    }
}

float BParam_GetValue(BParam* bp) {
    float range = bp->outMax - bp->outMin;
    return bp->slider.clipmaxF * range + bp->outMin;
}

void BParam_SetValue(BParam* bp, float val) {
    float range = bp->outMax - bp->outMin;
    if (range > 0.0001f)
        bp->slider.clipmaxF = (val - bp->outMin) / range;
    else
        bp->slider.clipmaxF = 1.0f;
    bp->slider.clipmaxF = fmaxf(0.0f, fminf(1.0f, bp->slider.clipmaxF));
}


