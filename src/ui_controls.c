#include "repaint.h"

static Texture2D penModeTex[PEN_MODE_COUNT];
static const char* PenIconNames[PEN_MODE_COUNT] = {
    "tct_none", "tct_pressure", "tct_vel", "tct_dir", "tct_rot", "tct_tilt",
    "tct_relang", "tct_htilt", "tct_vtilt", "tct_lenpx", "tct_acc",
    "tct_xtilt", "tct_ytilt"
};

static const char* PenModeNames[PEN_MODE_COUNT] = {
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
    slider->gradStart = (Color){0, 0, 0, 255};
    slider->gradEnd = (Color){255, 255, 255, 255};
    slider->shade = (Color){144, 144, 144, 255};
    slider->hlite = (Color){250, 250, 250, 255};
    slider->midtone = (Color){240, 240, 240, 255};
    slider->showValue = true;
    slider->noGradient = false;
    slider->label[0] = '\0';
    slider->prevDown[0] = false;
    slider->prevDown[1] = false;
    slider->prevDown[2] = false;
}

static void DualSlider_ParsePoint(DualSlider* slider, Vector2 mousePos) {
    if (slider->ActivePick < 0) return;

    float val;
    float w = slider->activeRect.width;
    float h = slider->activeRect.height;

    if (slider->orient == 0) {
        val = (mousePos.x - slider->activeRect.x) / w;
        val = fmaxf(0.0f, fminf(1.0f, val));

        bool snapZone = (mousePos.y > slider->activeRect.y + h * 2) || (mousePos.y < slider->activeRect.y - h);
        if (snapZone && slider->DsRange > 0) {
            val = roundf(val / slider->DsRange) * slider->DsRange;
            val = fmaxf(0.0f, fminf(1.0f, val));
        }
    } else {
        val = (mousePos.y - slider->activeRect.y) / h;
        val = fmaxf(0.0f, fminf(1.0f, val));
        val = 1.0f - val;

        bool snapZone = (mousePos.x > slider->activeRect.x + w * 2) || (mousePos.x < slider->activeRect.x - w);
        if (snapZone && slider->DsRange > 0) {
            val = roundf(val / slider->DsRange) * slider->DsRange;
            val = fmaxf(0.0f, fminf(1.0f, val));
        }
    }

    if (slider->ActivePick == 1) slider->clipmaxF = val;
    if (slider->ActivePick == 0) slider->clipminF = val;
    if (slider->ActivePick == 2) slider->jitter = val;
}

void DualSlider_HandleInput(DualSlider* slider, Vector2 mousePos) {
    bool left = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool right = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);
    bool middle = IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
    bool anyDown = left || right || middle;

    bool overSlider = CheckCollisionPointRec(mousePos, slider->rect);

    if (slider->ActivePick < 0 && !overSlider) {
        slider->prevDown[0] = false;
        slider->prevDown[1] = false;
        slider->prevDown[2] = false;
        return;
    }

    if (anyDown && !slider->prevDown[0] && !slider->prevDown[1] && !slider->prevDown[2]) {
        if (overSlider) {
            if (left) slider->ActivePick = 1;
            else if (right) slider->ActivePick = 0;
            else if (middle) slider->ActivePick = 2;
            slider->activeRect = slider->rect;
        }
    }

    if (!anyDown) slider->ActivePick = -1;

    if (slider->ActivePick >= 0) {
        DualSlider_ParsePoint(slider, mousePos);
    }

    slider->prevDown[0] = left;
    slider->prevDown[1] = right;
    slider->prevDown[2] = middle;
}

void Draw3DFrame(int x, int y, int w, int h, Color shd, Color hl) {
    DrawRectangle(x, y, w, 1, shd);
    DrawRectangle(x, y, 1, h, shd);
    DrawRectangle(x + w - 1, y + 1, 1, h - 1, hl);
    DrawRectangle(x + 1, y + h - 1, w - 1, 1, hl);
}

void DualSlider_Draw(DualSlider* slider) {
    Rectangle r = slider->rect;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    int Soff = slider->Soff;

    if (slider->label[0]) {
        DrawText(slider->label, x, y - 16, 12, WHITE);
    }

    if (!slider->noGradient)
        DrawRectangleGradientH(x, y, w, h, slider->gradStart, slider->gradEnd);

    if (slider->jitter > 0.0f) {
        if (slider->orient == 0) {
            int jw = (int)(w * slider->jitter);
            DrawRectangle(x, y, jw, 7, (Color){0, 0, 255, 255});
        } else {
            int jh = (int)(h * slider->jitter);
            DrawRectangle(x, y + h - jh, w, jh, (Color){0, 0, 255, 255});
        }
    }

    if (slider->orient == 0) {
        int hx = (int)(w * slider->clipminF) + x - slider->sliderrad;
        int hy = y + Soff;
        int hw = slider->sliderrad * 2;
        int hh = h - Soff * 2 - 1;
        DrawRectangle(hx, hy, hw, hh, slider->gradStart);
        Draw3DFrame(hx, hy, hw, hh, slider->shade, slider->midtone);

        hx = (int)(w * slider->clipmaxF) + x - slider->sliderrad;
        DrawRectangle(hx, hy, hw, hh, slider->gradEnd);
        Draw3DFrame(hx, hy, hw, hh, slider->midtone, slider->shade);
    } else {
        int hx = x + Soff;
        int hw = w - Soff * 2 - 1;
        int hy = (int)(h * (1.0f - slider->clipminF)) + y - slider->sliderrad;
        int hht = slider->sliderrad * 2;
        DrawRectangle(hx, hy, hw, hht, slider->gradStart);
        Draw3DFrame(hx, hy, hw, hht, slider->shade, slider->midtone);

        hy = (int)(h * (1.0f - slider->clipmaxF)) + y - slider->sliderrad;
        DrawRectangle(hx, hy, hw, hht, slider->gradEnd);
        Draw3DFrame(hx, hy, hw, hht, slider->midtone, slider->shade);
    }

    if (slider->showValue) {
        char valStr[16];
        sprintf(valStr, "%d", (int)(255.0f * slider->clipmaxF));
        int tw = MeasureText(valStr, 12);
        DrawText(valStr, x + (w - tw) / 2, y + (h - 12) / 2, 12, WHITE);
    }

    Draw3DFrame(x, y, w, h, slider->shade, slider->midtone);
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
            penModeTex[i] = (Texture2D){0};
        }
    }
}

void UnloadPenIcons(void) {
    for (int i = 0; i < PEN_MODE_COUNT; i++) {
        if (penModeTex[i].id > 0) UnloadTexture(penModeTex[i]);
    }
}

void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef) {
    DualSlider_Init(&bp->slider);
    bp->slider.clipmaxF = (outMax > outMin) ? (outDef - outMin) / (outMax - outMin) : 1.0f;
    bp->slider.clipmaxF = fmaxf(0.0f, fminf(1.0f, bp->slider.clipmaxF));
    bp->slider.showValue = false;
    bp->iconTex = (Texture2D){0};
    bp->iconLoaded = false;
    bp->penMode = csNone;
    bp->penEdit = false;
    bp->penPending = false;
    bp->outMin = outMin;
    bp->outMax = outMax;
    bp->id = id;
    strncpy(bp->name, name, sizeof(bp->name) - 1);
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

void BParam_Draw(BParam* bp) {
    Vector2 mp = GetMousePosition();
    DualSlider* sl = &bp->slider;
    Rectangle r = sl->rect;

    if (bp->name[0])
        DrawText(bp->name, (int)r.x, (int)r.y - 16, 12, DARKGRAY);

    Rectangle iconRect = {r.x - 28, r.y + (r.height - 24) / 2, 24, 24};
    if (bp->iconLoaded) {
        DrawTexturePro(bp->iconTex,
            (Rectangle){0, 0, (float)bp->iconTex.width, (float)bp->iconTex.height},
            iconRect, (Vector2){0, 0}, 0, WHITE);
    } else {
        DrawRectangleRec(iconRect, (Color){180, 180, 180, 255});
        DrawRectangleLinesEx(iconRect, 1, (Color){120, 120, 120, 255});
    }

    DualSlider_HandleInput(sl, mp);
    DualSlider_Draw(sl);
}

void BParam_DrawPen(BParam* bp) {
    Rectangle r = bp->slider.rect;
    int penX = (int)(r.x + r.width + 4);
    int penW = uiPanelWidth - 4 - penX;
    if (penW <= 28) return;

    Rectangle btnRect = {(float)penX, r.y, (float)penW, r.height};
    GuiButton(btnRect, "");

    if (bp->penMode >= 0 && bp->penMode < PEN_MODE_COUNT && penModeTex[bp->penMode].id > 0) {
        Texture2D* pt = &penModeTex[bp->penMode];
        int iconSize = (penW < r.height ? penW : (int)r.height) - 6;
        if (iconSize < 12) iconSize = 12;
        float s = fminf((float)iconSize / pt->width, (float)iconSize / pt->height);
        float dw = pt->width * s, dh = pt->height * s;
        Rectangle dst = {btnRect.x + (penW - dw) / 2, btnRect.y + (r.height - dh) / 2, dw, dh};
        DrawTexturePro(*pt, (Rectangle){0, 0, (float)pt->width, (float)pt->height}, dst, (Vector2){0, 0}, 0, WHITE);
    }

    if (CheckCollisionPointRec(GetMousePosition(), btnRect) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        bp->penPending = !bp->penPending;
}
