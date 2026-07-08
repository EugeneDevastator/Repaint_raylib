#include "ui_leftpanel.h"
#include "brush_blend.h"
#include "brush_preset.h"
#include "viewport_manager.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdint>

extern bool layersDirty;
extern bool panelResizing;
extern int uiPanelWidth;

Texture2D g_blendModeIcon = {0};
bool g_blendIconLoaded = false;

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
static int _userStart(void) { return 1; }
static int _separatorLine(void) { return 1 + g_presetUserCount; }
static int _defaultStart(void) { return _separatorLine() + 1; }
static int _totalListItems(void) { return 1 + g_presetUserCount + 1 + g_presetDefaultCount; }
static bool _isUserIndex(int listIdx) { return listIdx >= _userStart() && listIdx < _separatorLine(); }
static bool _isDefaultIndex(int listIdx) { return listIdx >= _defaultStart() && listIdx < _totalListItems(); }
static int _listToUser(int listIdx) { return listIdx - _userStart(); }
static int _listToDefault(int listIdx) { return listIdx - _defaultStart(); }

void LeftPanel_Init(void) {
    if (!g_blendIconLoaded) {
        if (FileExists("resources/ctlbm.png")) {
            Image img = LoadImage("resources/ctlbm.png");
            ImageResize(&img, 24, 24);
            g_blendModeIcon = LoadTextureFromImage(img);
            g_blendIconLoaded = g_blendModeIcon.id > 0;
            UnloadImage(img);
        }
    }
}

void LeftPanel_Shutdown(void) {
    if (g_blendIconLoaded) {
        UnloadTexture(g_blendModeIcon);
        g_blendIconLoaded = false;
    }
}

void LeftPanel_Draw(AppState* state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)uiPanelWidth, (float)GetScreenHeight()), ImGuiCond_Always);
    ImGui::Begin("Tools", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar);

    ImGui::Separator();
    ImGui::Text("Presets");
    ImGui::Spacing();

    // ── Brush presets panel ──
    {
        _loadPresets();

        // Button bar
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
                if (!exists && g_presetUserCount < BRUSH_PRESET_MAX) {
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
            } else { DisplayInfoText("Cannot overwrite"); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(btnW, 0))) {
            if (g_presetSelected > 0 && _isUserIndex(g_presetSelected)) {
                int ui = _listToUser(g_presetSelected);
                for (int i = ui; i < g_presetUserCount - 1; i++) g_presetUser[i] = g_presetUser[i + 1];
                g_presetUserCount--; g_presetSelected = -1; g_presetNameBuf[0] = '\0';
                _saveUserPresets();
                DisplayInfoText("Deleted");
            } else { DisplayInfoText("Cannot delete"); }
        }

        // Name text field
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##pname", g_presetNameBuf, BRUSH_PRESET_NAME_MAX);

        // Preset list
        int total = _totalListItems();
        float listH = fminf(ImGui::GetContentRegionAvail().y, GetScreenHeight() / 3.0f);
        if (listH < 20) listH = 20;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
        ImGui::BeginChild("##plist", ImVec2(0, listH), ImGuiChildFlags_Borders);
        for (int li = 0; li < total; li++) {
            if (li == _separatorLine()) { ImGui::Separator(); continue; }
            const char* label = NULL;
            if (li == 0) label = "Last Unsaved";
            else if (_isUserIndex(li)) label = g_presetUser[_listToUser(li)].name;
            else if (_isDefaultIndex(li)) label = g_presetDefault[_listToDefault(li)].name;
            if (!label) continue;
            ImGui::PushID(li);
            bool isSel = (li == g_presetSelected);
            if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_AllowDoubleClick)) {
                g_presetSelected = li;
                if (ImGui::IsMouseDoubleClicked(0) && li > 0) {
                    if (_isUserIndex(li)) Preset_ApplyToCurrent(&g_presetUser[_listToUser(li)], state);
                    else if (_isDefaultIndex(li)) Preset_ApplyToCurrent(&g_presetDefault[_listToDefault(li)], state);
                    snprintf(g_presetNameBuf, sizeof(g_presetNameBuf), "%s", label);
                    DisplayInfoText("Applied");
                } else if (li > 0) { snprintf(g_presetNameBuf, sizeof(g_presetNameBuf), "%s", label); }
                else { g_presetNameBuf[0] = '\0'; }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Blend mode (selectable list, immediate highlight on mouse down)
    {
        static const char* names[] = {
            "N-Gamma","N-OKLab","N-Linear","Overlay","EraseA","EraseColor",
            "Darken","Lighten","Multiply","Screen","Burn","Color Dodge",
            "Luminosity","Color","LinLight","Saturation","LinDodge"
        };
        static const int map[] = {0,2,1,11,3,4,8,7,10,5,9,6,13,12,16,14,15};
        int n = sizeof(map) / sizeof(map[0]);
        int sel = 0;
        uint8_t curBm = state->currentBrush.Realb.bmidx;
        for (int i = 0; i < n; i++)
            if (map[i] == (int)curBm) { sel = i; break; }
        bool changed = DrawSelector("Blend Mode", &sel, names, n, 2);
        if (changed)
            state->currentBrush.Realb.bmidx = (uint8_t)map[sel];
        else
            state->currentBrush.Realb.bmidx = (uint8_t)map[sel];

        if (changed && map[sel] == 4)
            DisplayInfoText("Color erase: colored = chroma key, gray = alpha paint");
    }

    ImGui::Spacing();
    bool seamlessMode = g_seamlessPaint || g_seamlessPreview;
    if (ImGui::Checkbox("Seamless Mode", &seamlessMode)) {
        g_seamlessPaint = seamlessMode;
        g_seamlessPreview = seamlessMode;
    }

    ImGui::Spacing();
    int preserve = state->currentBrush.Realb.preserveop;
    ImGui::Checkbox("Preserve Layer Alpha", (bool*)&preserve);
    state->currentBrush.Realb.preserveop = (uint8_t)preserve;

    extern bool g_pixelPerfect;
    ImGui::Checkbox("Pixel Perfect", &g_pixelPerfect);

    extern int g_strokeSmoothingMode;
    extern float g_strokeThrottle;
    ImGui::Text("Smoothing");
    ImGui::RadioButton("Linear", &g_strokeSmoothingMode, SMOOTH_MODE_LINEAR); ImGui::SameLine();
    ImGui::RadioButton("Smooth", &g_strokeSmoothingMode, SMOOTH_MODE_SMOOTH);

    if (g_strokeSmoothingMode == SMOOTH_MODE_SMOOTH) {
        ImGui::Indent(10);
        ImGui::SetNextItemWidth(-15);
        ImGui::SliderFloat("Throttle", &g_strokeThrottle, 0.0f, 100.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higher values = fewer segment endpoints = smoother curved strokes");
        ImGui::Unindent(10);
    }

    ImGui::Separator();
    ImGui::Text("Debug");
    ImGui::Spacing();

    // ── Test broker ──
    //extern bool g_useTestBroker;
    //ImGui::Checkbox("Test Broker (+200px X)", &g_useTestBroker);

    // Zoom info
    /*{
        char zoomInfo[32];
        sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
        ImGui::Text("%s", zoomInfo);
    }*/

    if (ImGui::Button("Reload Shaders", ImVec2(-1, 0))) {
        BrushBlend_Shutdown();
        BrushBlend_Init();
        ViewportManager_ReloadShader();
    }

    /*if (ImGui::Button("Changelog", ImVec2(-1, 0))) {
        Changelog_Toggle();
    }*/

    // Separator + resize handle at right edge (drawn inside ImGui for proper z-order)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        float handleX = wMin.x + wSize.x;
        ImU32 col = panelResizing ? IM_COL32(80, 120, 200, 255) : IM_COL32(160, 160, 160, 255);
        float sh = (float)GetScreenHeight();
        dl->AddRectFilled(ImVec2(handleX - 3, wMin.y), ImVec2(handleX + 4, wMin.y + sh), col);

        // Invisible button for resize interaction
        ImGui::SetCursorScreenPos(ImVec2(handleX - 3, wMin.y));
        ImGui::InvisibleButton("##resize", ImVec2(7, sh));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            panelResizing = true;
            float mx = ImGui::GetMousePos().x;
            uiPanelWidth = (int)fmaxf(120.0f, fminf(mx, (float)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH - 100)));
            Rectangle vb = {(float)uiPanelWidth, 0,
                (float)(GetScreenWidth() - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)GetScreenHeight()};
            Viewport_SetBounds(&viewport, vb);
        } else {
            panelResizing = false;
        }
    }

    ImGui::End();
}

// ── LeftPanelModule ───────────────────────────────────────────────────────

bool LeftPanelModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (input.mouseCaptured) return false;
    if (!rect.Contains(input.MousePos())) return false;
    input.mouseCaptured = true;
    return true;
}

void LeftPanelModule::DrawGUI(const DrawRect& rect) {
    if (rect.w < 1 || rect.h < 1) return;
    LeftPanel_Draw(state);
}
