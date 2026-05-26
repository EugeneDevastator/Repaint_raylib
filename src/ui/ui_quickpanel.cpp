#include "repaint.h"
#include "brush_preset.h"
#include "rlgl.h"
#include "imgui.h"
#include <math.h>
#include <algorithm>

void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Draw(AppState* state, Rectangle vp);

int quickPanelMouseMode = 0;
bool g_colorPicking = false;
Color g_colorPickGrid[25] = {};

// Debug logging - first open flag
static bool g_quickPanelTexLogged = false;

// ── Brush presets state ───────────────────────────────────────────────
static BrushPreset g_presetUser[BRUSH_PRESET_MAX];
static BrushPreset g_presetDefault[BRUSH_PRESET_MAX];
static int g_presetUserCount = 0;
static int g_presetDefaultCount = 0;
static bool g_presetLoaded = false;
static char g_presetNameBuf[BRUSH_PRESET_NAME_MAX] = "";
static int g_presetSelected = -1;

static int _sortPresetByName(const void* a, const void* b) {
    return strcmp(((const BrushPreset*)a)->name, ((const BrushPreset*)b)->name);
}

static void _loadPresets(void) {
    if (g_presetLoaded) return;
    g_presetLoaded = true;
    int u = Preset_LoadUser(g_presetUser, BRUSH_PRESET_MAX);
    g_presetUserCount = (u > BRUSH_PRESET_MAX) ? BRUSH_PRESET_MAX : u;
    qsort(g_presetUser, g_presetUserCount, sizeof(BrushPreset), _sortPresetByName);

    int d = Preset_LoadDefault(g_presetDefault, BRUSH_PRESET_MAX);
    g_presetDefaultCount = (d > BRUSH_PRESET_MAX) ? BRUSH_PRESET_MAX : d;
    qsort(g_presetDefault, g_presetDefaultCount, sizeof(BrushPreset), _sortPresetByName);
}

static void _saveUserPresets(void) {
    Preset_SaveUser(g_presetUser, g_presetUserCount);
}

// ── Preset list helpers ──
// List layout: [0 = Last Unsaved] [1..U = user sorted] [---] [U+1..U+D = default sorted]
static int _userStart(void) { return 1; }
static int _separatorLine(void) { return 1 + g_presetUserCount; }
static int _defaultStart(void) { return _separatorLine() + 1; }
static int _totalListItems(void) {
    // Last unsaved + user count + separator + default count
    return 1 + g_presetUserCount + 1 + g_presetDefaultCount;
}

static bool _isUserIndex(int listIdx) {
    return listIdx >= _userStart() && listIdx < _separatorLine();
}
static bool _isDefaultIndex(int listIdx) {
    return listIdx >= _defaultStart() && listIdx < _totalListItems();
}
static int _listToUser(int listIdx) { return listIdx - _userStart(); }
static int _listToDefault(int listIdx) { return listIdx - _defaultStart(); }

void QuickPanel_Init(void) {
    FilePanel_Init();
    ToolBox_Init();
}

void QuickPanel_Shutdown(void) {
    FilePanel_Shutdown();
    ToolBox_Shutdown();
}

void QuickPanel_DrawUI(AppState* state) {
    if (g_activeHud != HUD_QUICK) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    rlSetBlendMode(RL_BLEND_ALPHA);
    FilePanel_Draw(state, vp);
    rlSetBlendMode(RL_BLEND_ALPHA);
    ToolBox_Draw(state, vp);

    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, vp.height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    rlSetBlendMode(RL_BLEND_ALPHA);
    ImGui::Begin("##qpui", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int gizR = 200;
    float dThick = fmaxf(20.0f, fminf(sw / 12.0f, 48.0f));
    float dLen   = fmaxf(100.0f, fminf(sh / 3.0f, 400.0f));
    int dCtrl    = (int)dThick;
    int dGap     = fmaxf(4, dCtrl / 3);
    int dSpacing = fmaxf(2, dCtrl / 6);

    int totalColH = dCtrl + dSpacing + (int)dLen + dSpacing + dCtrl;
    int sliderLeftX = gcx - gizR - 3 * dCtrl - 2 * dGap - 12;
    int sliderRightX = gcx + gizR + 12;
    int penBtnY = gcy - totalColH / 2;
    int slY = penBtnY + dCtrl + dSpacing;
    int iconY = slY + (int)dLen + dSpacing;

    // ── Brush presets panel ──
    {
        _loadPresets();
        // Fit between viewport left edge and left slider column, capped at 180px
        int panelW = sliderLeftX - (int)vp.x - dGap * 2;
        if (panelW > 180) panelW = 180;
        if (panelW < 80)  panelW = 80;
        int panelX = sliderLeftX - dGap - panelW;
        int panelTop = penBtnY - 14;
        int panelBot = iconY + dCtrl;
        int panelH = panelBot - panelTop;

        ImGui::SetCursorScreenPos(ImVec2(panelX, panelTop));
        ImGui::BeginChild("##presets", ImVec2((float)panelW, (float)panelH), true,
            ImGuiWindowFlags_NoScrollbar);

        // ── Button bar (top) ──
        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (btnW < 20) btnW = 20;

        if (ImGui::Button("Add", ImVec2(btnW, 0))) {
            if (g_presetNameBuf[0]) {
                BrushPreset tmp;
                Preset_CaptureFromCurrent(&tmp, state);
                snprintf(tmp.name, sizeof(tmp.name), "%s", g_presetNameBuf);
                bool exists = false;
                for (int i = 0; i < g_presetUserCount; i++) {
                    if (strcmp(g_presetUser[i].name, tmp.name) == 0) { exists = true; break; }
                }
                if (exists) {
                    int suffix = 2;
                    char tryName[BRUSH_PRESET_NAME_MAX];
                    for (; suffix < 9999; suffix++) {
                        snprintf(tryName, sizeof(tryName), "%.48s %d", g_presetNameBuf, suffix);
                        bool found = false;
                        for (int i = 0; i < g_presetUserCount; i++) {
                            if (strcmp(g_presetUser[i].name, tryName) == 0) { found = true; break; }
                        }
                        if (!found) break;
                    }
                    memcpy(tmp.name, tryName, BRUSH_PRESET_NAME_MAX);
                    memcpy(g_presetNameBuf, tryName, BRUSH_PRESET_NAME_MAX);
                }
                if (g_presetUserCount < BRUSH_PRESET_MAX) {
                    g_presetUser[g_presetUserCount++] = tmp;
                    qsort(g_presetUser, g_presetUserCount, sizeof(BrushPreset), _sortPresetByName);
                    _saveUserPresets();
                    DisplayInfoText("Added");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Write", ImVec2(btnW, 0))) {
            if (g_presetSelected > 0 && _isUserIndex(g_presetSelected)) {
                int ui = _listToUser(g_presetSelected);
                Preset_CaptureFromCurrent(&g_presetUser[ui], state);
                snprintf(g_presetUser[ui].name, sizeof(g_presetUser[ui].name), "%s", g_presetNameBuf);
                qsort(g_presetUser, g_presetUserCount, sizeof(BrushPreset), _sortPresetByName);
                _saveUserPresets();
                DisplayInfoText("Written");
            } else {
                DisplayInfoText("Cannot overwrite");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(btnW, 0))) {
            if (g_presetSelected > 0 && _isUserIndex(g_presetSelected)) {
                int ui = _listToUser(g_presetSelected);
                for (int i = ui; i < g_presetUserCount - 1; i++)
                    g_presetUser[i] = g_presetUser[i + 1];
                g_presetUserCount--;
                g_presetSelected = -1;
                g_presetNameBuf[0] = '\0';
                _saveUserPresets();
                DisplayInfoText("Deleted");
            } else {
                DisplayInfoText("Cannot delete");
            }
        }

        // ── Name text field ──
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##pname", g_presetNameBuf, BRUSH_PRESET_NAME_MAX);

        // ── Preset list ──
        int total = _totalListItems();
        float listH = ImGui::GetContentRegionAvail().y;
        if (listH < 20) listH = 20;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
        ImGui::BeginChild("##plist", ImVec2(0, listH), ImGuiChildFlags_Borders);
        for (int li = 0; li < total; li++) {
            if (li == _separatorLine()) {
                ImGui::Separator();
                continue;
            }
            const char* label = NULL;
            if (li == 0) {
                label = "Last Unsaved";
            } else if (_isUserIndex(li)) {
                label = g_presetUser[_listToUser(li)].name;
            } else if (_isDefaultIndex(li)) {
                label = g_presetDefault[_listToDefault(li)].name;
            }
            if (!label) continue;

            ImGui::PushID(li);
            bool isSel = (li == g_presetSelected);
            if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
                g_presetSelected = li;
                if (ImGui::IsMouseDoubleClicked(0) && li > 0) {
                    if (_isUserIndex(li))
                        Preset_ApplyToCurrent(&g_presetUser[_listToUser(li)], state);
                    else if (_isDefaultIndex(li))
                        Preset_ApplyToCurrent(&g_presetDefault[_listToDefault(li)], state);
                    snprintf(g_presetNameBuf, sizeof(g_presetNameBuf), "%s", label);
                    DisplayInfoText("Applied");
                } else if (li > 0) {
                    snprintf(g_presetNameBuf, sizeof(g_presetNameBuf), "%s", label);
                } else {
                    g_presetNameBuf[0] = '\0';
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::EndChild();
    }

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};


    rlSetBlendMode(RL_BLEND_ALPHA);
    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (dCtrl + dGap)
            : sliderRightX + (i - 3) * (dCtrl + dGap);
        BParam* bp = bps[i];

        dl->AddText(ImVec2(colX + dCtrl / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

        ImGui::SetCursorScreenPos(ImVec2(colX, penBtnY));
        Texture2D pt = GetPenModeIcon(bp->penMode);
        ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;

        ImGui::PushID(300 + i);
        char pname[32];
        snprintf(pname, sizeof(pname), "penpop_%d", i);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (penTid) {
            if (ImGui::ImageButton("##pb", penTid, ImVec2(dCtrl, dCtrl)))
                ImGui::OpenPopup(pname);
        } else {
            if (ImGui::Button("...", ImVec2(dCtrl, dCtrl)))
                ImGui::OpenPopup(pname);
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        rlSetBlendMode(RL_BLEND_ALPHA);
        if (ImGui::BeginPopup(pname, ImGuiWindowFlags_NoScrollbar)) {
            for (int p = 0; p < PEN_MODE_COUNT; p++) {
                Texture2D itex = GetPenModeIcon(p);
                if (itex.id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)itex.id, ImVec2(16, 16));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(PenModeNames[p], bp->penMode == p))
                    bp->penMode = p;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        rlSetBlendMode(RL_BLEND_ALPHA);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
        DrawSlider(bp, 1, (float)dCtrl, (float)dLen);

        if (bp->iconLoaded) {
            ImTextureID iconTid = (ImTextureID)(intptr_t)bp->iconTex.id;
            if (iconTid) {
                ImGui::SetCursorScreenPos(ImVec2(colX, iconY));
                ImGui::Image(iconTid, ImVec2(dCtrl, dCtrl));
            }
        } else {
            Color swatch = (i == 3) ? HSLToRGB(colorHue, 1.0f, 0.5f)
                        : (i == 4) ? HSLToRGB(colorHue, colorSat, colorLit)
                        : HSLToRGB(colorHue, colorSat, colorLit);
            ImU32 swCol = IM_COL32(swatch.r, swatch.g, swatch.b, 255);
            dl->AddRectFilled(ImVec2(colX, iconY), ImVec2(colX + dCtrl, iconY + dCtrl), swCol);
            dl->AddRect(ImVec2(colX, iconY), ImVec2(colX + dCtrl, iconY + dCtrl), IM_COL32(200, 200, 200, 200));
        }
    }

     {
          int texAreaY = iconY + dCtrl + 14;
         int texCount = state->brushTexCount;

         float thirdW = vp.width / 3.0f;
         float yPos = (float)texAreaY;

         // ── Left 1/3: 4 radio columns with 2px gap ──
         int mm = state->currentBrush.Realb.useTexLumAsAlpha ? 0 : 1;
         int mx = state->currentBrush.Realb.texBlendMode;
         int tnm = state->currentBrush.Realb.texNoisemode;
         int cm = state->currentBrush.Realb.texColorMode;

         float colW = (float)uiPanelWidth * 0.6f;

         rlSetBlendMode(RL_BLEND_ALPHA);
         ImGui::SetCursorScreenPos(ImVec2(vp.x, yPos));
         ImGui::BeginChild("##texLeft", ImVec2(thirdW, 0), false);
         {
             ImGui::SetCursorPos(ImVec2(10, 10));
             float x = 10;

              static const char* items0[] = {"lum is alpha", "tex.a is alpha"};
              static const char* items1[] = {"multiply", "threshold", "use tex mask"};
              static const char* items2[] = {"Stencil", "Random", "Const"};
              static const char* items3[] = {"brush RGB", "texture RGB", "mul brush*tex"};

              ImGui::SetCursorPos(ImVec2(x, 10));
              ImGui::BeginChild("##rg0", ImVec2(colW, 0), false);
              { int v = mm; DrawRadioGroup("Mask Mode", &v, items0, 2); mm = v; }
              ImGui::EndChild();
              x += colW + 2.0f;

              ImGui::SetCursorPos(ImVec2(x, 10));
              ImGui::BeginChild("##rg1", ImVec2(colW, 0), false);
              { int v = mx; DrawRadioGroup("Mask Mix", &v, items1, 3); mx = v; }
              ImGui::EndChild();
              x += colW + 2.0f;

              ImGui::SetCursorPos(ImVec2(x, 10));
              ImGui::BeginChild("##rg2", ImVec2(colW, 0), false);
              { int v = tnm; DrawRadioGroup("Sample Mode", &v, items2, 3); tnm = v; }
              ImGui::EndChild();

              // Color mode on next line
              ImGui::SetCursorPos(ImVec2(10, 120));
              ImGui::BeginChild("##rg3", ImVec2(colW, 0), false);
              { int v = cm; DrawRadioGroup("Color", &v, items3, 3); cm = v; }
              ImGui::EndChild();
          }
          ImGui::EndChild();

          state->currentBrush.Realb.useTexLumAsAlpha = (mm == 0);
         state->currentBrush.Realb.texBlendMode = mx;
         state->currentBrush.Realb.texNoisemode = tnm;
         state->currentBrush.Realb.texColorMode = cm;

         // ── Middle 1/3: texture sliders ──
         rlSetBlendMode(RL_BLEND_ALPHA);
         ImGui::SetCursorScreenPos(ImVec2(vp.x + thirdW, yPos));
         ImGui::BeginChild("##texMiddle", ImVec2(thirdW, 0), false);
         ImGui::SetCursorPos(ImVec2(10, 10));
         DrawSlider(&bpTexScale, 0);
         rlSetBlendMode(RL_BLEND_ALPHA);
         DrawSlider(&bpTexFeather, 0);
         rlSetBlendMode(RL_BLEND_ALPHA);
         DrawSlider(&bpTexThresh, 0);
         rlSetBlendMode(RL_BLEND_ALPHA);
         DrawSlider(&bpTexBlendVal, 0);
         ImGui::EndChild();

         // ── Right 1/3: thumbnail grid ──
         rlSetBlendMode(RL_BLEND_ALPHA);
         ImGui::SetCursorScreenPos(ImVec2(vp.x + 2.0f * thirdW, yPos));
         ImGui::BeginChild("##texRight", ImVec2(thirdW, 0), false);
         {
             ImGui::SetCursorPos(ImVec2(10, 10));
             int texCols = 3;
             int texSz = 64;
             int texGap = 6;

             ImGui::PushID("500");
             bool isNone = (state->activeBrushTex < 0);
             if (isNone) { ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1)); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.4f,0.4f,1)); }
             if (ImGui::Button("X", ImVec2(texSz, texSz)))
                 BrushTex_SetActive(state, -1);
             if (isNone) { ImGui::PopStyleColor(2); }
             ImGui::PopID();

             rlSetBlendMode(RL_BLEND_ALPHA);
             for (int ti = 0; ti < texCount && ti < texCols * 6; ti++) {
                 int col = ti % texCols;
                 int row = ti / texCols;
                 float tx = 10 + col * (texSz + texGap);
                 float ty = 10 + row * (texSz + texGap) + texSz + texGap;
                 ImGui::SetCursorPos(ImVec2(tx, ty));
                 ImGui::PushID(501 + ti);
                 Texture2D thumb = BrushTex_GetThumb(state, ti);
                 bool isSel = (state->activeBrushTex == ti);
                 if (isSel) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1));
                 if (thumb.id > 0) {
                     if (!g_quickPanelTexLogged) {
                         TraceLog(LOG_INFO, "[QuickPanel] Tex[%d] name='%s' thumb.id=%d rt.id=%d",
                             ti, state->brushTex[ti].name,
                             thumb.id, state->brushTex[ti].rt.id);
                         g_quickPanelTexLogged = true;
                     }
                     if (ImGui::ImageButton("##bt", (ImTextureID)(intptr_t)thumb.id, ImVec2(texSz, texSz)))
                         BrushTex_SetActive(state, ti);
                 }
                 if (isSel) ImGui::PopStyleColor();
                 ImGui::PopID();
             }
         }
         ImGui::EndChild();
      }

      ImGui::End();
  }
