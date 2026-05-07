#include "repaint.h"

#define POPUP_ITEM_H 20
#define POPUP_WIDTH 140
#define POPUP_ICON_S 16

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

static BParam* activePopup = NULL;

void UIButton_Update(UIButton* btn, Vector2 mousePos, bool mousePressed) {
    btn->hovered = CheckCollisionPointRec(mousePos, btn->rect);
    if (btn->hovered && mousePressed && !btn->clicked) {
        btn->clicked = true;
    }
}

void UIButton_Draw(UIButton* btn) {
    Color drawColor = btn->hovered ? btn->hoverColor : btn->color;
    DrawRectangleRec(btn->rect, drawColor);
    DrawRectangleLinesEx(btn->rect, 1, DARKGRAY);
    int textX = btn->rect.x + (btn->rect.width - MeasureText(btn->label, 16)) / 2;
    int textY = btn->rect.y + (btn->rect.height - 16) / 2;
    DrawText(btn->label, textX, textY, 16, WHITE);
}

void UISlider_Init(UISlider* slider) {
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
    slider->id = 0;
    slider->prevDown[0] = false;
    slider->prevDown[1] = false;
    slider->prevDown[2] = false;
}

static void UISlider_ParsePoint(UISlider* slider, Vector2 mousePos) {
    if (slider->ActivePick < 0) return;

    float val;
    float w = slider->rect.width;
    float h = slider->rect.height;

    if (slider->orient == 0) {
        val = (mousePos.x - slider->rect.x) / w;
        val = fmaxf(0.0f, fminf(1.0f, val));

        bool snapZone = (mousePos.y > slider->rect.y + h * 2) || (mousePos.y < slider->rect.y - h);
        if (snapZone && slider->DsRange > 0) {
            val = roundf(val / slider->DsRange) * slider->DsRange;
            val = fmaxf(0.0f, fminf(1.0f, val));
        }
    } else {
        val = (mousePos.y - slider->rect.y) / h;
        val = fmaxf(0.0f, fminf(1.0f, val));
        val = 1.0f - val;
        bool snapZone = (mousePos.x > slider->rect.x + w * 2) || (mousePos.x < slider->rect.x - w);
        if (snapZone && slider->DsRange > 0) {
            val = roundf(val / slider->DsRange) * slider->DsRange;
            val = fmaxf(0.0f, fminf(1.0f, val));
        }
    }

    if (slider->ActivePick == 1) slider->clipmaxF = val;
    if (slider->ActivePick == 0) slider->clipminF = val;
    if (slider->ActivePick == 2) slider->jitter = val;
}

void UISlider_HandleInput(UISlider* slider, Vector2 mousePos) {
    bool left = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool right = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);
    bool middle = IsMouseButtonDown(MOUSE_MIDDLE_BUTTON);
    bool anyDown = left || right || middle;

    bool overSlider = CheckCollisionPointRec(mousePos, slider->rect);

    if (anyDown && !slider->prevDown[0] && !slider->prevDown[1] && !slider->prevDown[2]) {
        if (overSlider) {
            if (left) slider->ActivePick = 1;
            else if (right) slider->ActivePick = 0;
            else if (middle) slider->ActivePick = 2;
        }
    }

    if (!anyDown) slider->ActivePick = -1;

    if (slider->ActivePick >= 0) {
        UISlider_ParsePoint(slider, mousePos);
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

void UISlider_Draw(UISlider* slider) {
    Rectangle r = slider->rect;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    int Soff = slider->Soff;

    if (slider->label[0]) {
        DrawText(slider->label, x, y - 16, 12, WHITE);
    }

    if (!slider->noGradient)
        DrawRectangleGradientH(x, y, w, h, slider->gradStart, slider->gradEnd);

    if (slider->jitter > 0.0f) {
        int jw = (int)(w * slider->jitter);
        DrawRectangle(x, y, jw, 7, (Color){0, 0, 255, 255});
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
    UISlider_Init(&bp->slider);
    bp->iconTex = (Texture2D){0};
    bp->iconLoaded = false;
    bp->penMode = csNone;
    bp->penState = PEN_STATE_OFF;
    bp->penRect = (Rectangle){0, 0, 28, 28};
    bp->outMin = outMin;
    bp->outMax = outMax;
    bp->outDef = outDef;
    bp->id = id;
    bp->popupActive = false;
    bp->popupHover = -1;
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
    UISlider* sl = &bp->slider;
    Rectangle r = sl->rect;

    if (bp->name[0])
        DrawText(bp->name, (int)r.x, (int)r.y - 16, 12, WHITE);

    Rectangle iconRect = {r.x - 28, r.y + (r.height - 24) / 2, 24, 24};
    if (bp->iconLoaded) {
        DrawTexturePro(bp->iconTex,
            (Rectangle){0, 0, (float)bp->iconTex.width, (float)bp->iconTex.height},
            iconRect, (Vector2){0, 0}, 0, WHITE);
    } else {
        DrawRectangleRec(iconRect, (Color){60, 60, 60, 255});
        DrawRectangleLinesEx(iconRect, 1, DARKGRAY);
    }

    UISlider_Draw(sl);

    Rectangle pr = bp->penRect;
    bool hover = CheckCollisionPointRec(mp, pr);
    Color btnCol = hover ? (Color){80, 80, 90, 255} : (Color){60, 60, 70, 255};
    if (bp->penState == PEN_STATE_OFF) btnCol = (Color){50, 50, 55, 255};

    DrawRectangleRec(pr, btnCol);
    DrawRectangleLinesEx(pr, 1, (Color){100, 100, 110, 255});

    if (bp->penMode >= 0 && bp->penMode < PEN_MODE_COUNT && penModeTex[bp->penMode].id > 0) {
        Texture2D* pt = &penModeTex[bp->penMode];
        float scale = fminf(pr.width / (float)pt->width, pr.height / (float)pt->height) * 0.8f;
        float dw = pt->width * scale, dh = pt->height * scale;
        Rectangle dst = {pr.x + (pr.width - dw) / 2, pr.y + (pr.height - dh) / 2, dw, dh};
        Color tint = bp->penState == PEN_STATE_OFF ? (Color){100, 100, 100, 255} : WHITE;
        DrawTexturePro(*pt,
            (Rectangle){0, 0, (float)pt->width, (float)pt->height},
            dst, (Vector2){0, 0}, 0, tint);
    }

    if (!bp->popupActive) return;

    float pw = POPUP_WIDTH;
    float ph = PEN_MODE_COUNT * POPUP_ITEM_H + 4;
    float px = pr.x + pr.width - pw;
    float py = pr.y + pr.height + 2;
    if ((int)(py + ph) > GetScreenHeight() - 10)
        py = pr.y - ph - 2;

    Rectangle popupRect = {px, py, pw, ph};

    DrawRectangleRec(popupRect, (Color){45, 45, 50, 255});
    DrawRectangleLinesEx(popupRect, 1, (Color){120, 120, 130, 255});

    for (int i = 0; i < PEN_MODE_COUNT; i++) {
        float iy = py + 2 + i * POPUP_ITEM_H;
        Rectangle itemRect = {px + 2, iy, pw - 4, POPUP_ITEM_H};

        if (i == bp->popupHover)
            DrawRectangleRec(itemRect, (Color){70, 70, 95, 255});

        if (penModeTex[i].id > 0) {
            float ds = POPUP_ICON_S;
            float is = fminf((float)penModeTex[i].width, (float)penModeTex[i].height);
            Rectangle src = {0, 0, is, is};
            Rectangle dst = {itemRect.x + 4, itemRect.y + (POPUP_ITEM_H - ds) / 2, ds, ds};
            DrawTexturePro(penModeTex[i], src, dst, (Vector2){0, 0}, 0, WHITE);
        }

        DrawText(PenModeNames[i], (int)(itemRect.x + POPUP_ICON_S + 8),
                 (int)(itemRect.y + (POPUP_ITEM_H - 10) / 2), 10,
                 i == bp->popupHover ? WHITE : (Color){180, 180, 180, 255});
    }
}

void BParam_HandleInput(BParam* bp, Vector2 mousePos) {
    if (activePopup != NULL && activePopup != bp) return;

    if (bp->popupActive) {
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) || IsKeyPressed(KEY_ESCAPE)) {
            bp->popupActive = false;
            bp->popupHover = -1;
            activePopup = NULL;
            return;
        }

        float pw = POPUP_WIDTH;
        float ph = PEN_MODE_COUNT * POPUP_ITEM_H + 4;
        Rectangle pr = bp->penRect;
        float px = pr.x + pr.width - pw;
        float py = pr.y + pr.height + 2;
        if ((int)(py + ph) > GetScreenHeight() - 10)
            py = pr.y - ph - 2;
        Rectangle popupRect = {px, py, pw, ph};
        Rectangle outerRect = {px - 10, py - 10, pw + 20, ph + 20};

        if (CheckCollisionPointRec(mousePos, popupRect)) {
            int relY = (int)(mousePos.y - py - 2);
            bp->popupHover = relY / POPUP_ITEM_H;
            if (bp->popupHover < 0 || bp->popupHover >= PEN_MODE_COUNT)
                bp->popupHover = -1;
        } else {
            bp->popupHover = -1;
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (bp->popupHover >= 0)
                bp->penMode = bp->popupHover;
            bp->popupActive = false;
            activePopup = NULL;
            return;
        }

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mousePos, outerRect)) {
            bp->popupActive = false;
            bp->popupHover = -1;
            activePopup = NULL;
            return;
        }

        return;
    }

    UISlider_HandleInput(&bp->slider, mousePos);

    if (CheckCollisionPointRec(mousePos, bp->penRect)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            bp->popupActive = true;
            bp->popupHover = bp->penMode;
            activePopup = bp;
        }
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            bp->penState = (bp->penState == PEN_STATE_OFF) ? PEN_STATE_DIRECT : PEN_STATE_OFF;
        }
    }
}
