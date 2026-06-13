#include "repaint.h"
#include "imgui.h"

// ── Shared blend mode names ──────────────────────────────────────────
const char* g_blendModeNames[] = {
    "N-Gamma","N-Linear","N-OKLab","EraseA","EraseColor","Screen",
    "Color Dodge","Lighten","Darken","Burn","Multiply","Overlay","Color",
    "Luminosity","Saturation"
};
extern const int g_blendModeCount = 15;

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
    slider->DsRange = 0.25f;
    slider->ActivePick = -1;
    slider->orient = 0;
    slider->Soff = 2;
    slider->sliderrad = 2;
    slider->colorMode = -1;
    slider->gradStart = Color{0, 0, 0, 255};
    slider->gradEnd = Color{255, 255, 255, 255};
    slider->shade = Color{144, 144, 144, 255};
    slider->hlite = Color{250, 250, 250, 255};
    slider->midtone = Color{240, 240, 240, 255};
    slider->showValue = true;
    slider->noGradient = false;
    slider->label[0] = '\0';
}



void LoadPenIcons(void) {
    for (int i = 0; i < PEN_MODE_COUNT; i++) {
        char path[128];
        sprintf(path, "resources/%s.png", PenIconNames[i]);
        if (FileExists(path)) {
            Image img = LoadImage(path);
            ImageResize(&img, 29, 29);
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
    bp->user.clipminF = 0.0f;
    bp->user.clipmaxF = bp->defClipmaxF;
    bp->user.jitter = 0.0f;
    bp->run.clipminF = 0.0f;
    bp->run.clipmaxF = bp->defClipmaxF;
    bp->slider.showValue = false;
    bp->iconTex = Texture2D{0};
    bp->iconLoaded = false;
    bp->penMode = csNone;
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
        ImageResize(&img, 29, 29);
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
    return bp->user.clipmaxF * range + bp->outMin;
}

void BParam_SetValue(BParam* bp, float val) {
    float range = bp->outMax - bp->outMin;
    if (range > 0.0001f)
        bp->user.clipmaxF = (val - bp->outMin) / range;
    else
        bp->user.clipmaxF = 1.0f;
    bp->user.clipmaxF = fmaxf(0.0f, fminf(1.0f, bp->user.clipmaxF));
}

void BParam_SnapRunState(BParam* bp) {
    bp->run.clipminF = bp->user.clipminF;
    bp->run.clipmaxF = bp->user.clipmaxF;
}

void DrawRadioGroup(const char* label, int* current, const char* items[], int itemCount) {
    if (label) ImGui::Text("%s", label);

    float totalW = ImGui::GetContentRegionAvail().x;

    for (int i = 0; i < itemCount; i++) {
        ImGui::PushID(i);

        bool isSel = (*current == i);
        ImVec4 bg = isSel
            ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
            : ImVec4(0.78f, 0.78f, 0.78f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

        if (ImGui::Button(items[i], ImVec2(totalW, 0)))
            *current = i;

        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }
}


