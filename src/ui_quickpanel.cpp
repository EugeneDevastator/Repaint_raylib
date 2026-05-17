#include "repaint.h"
#include "rlgl.h"
#include "imgui.h"
#include <math.h>

// Sub-component declaration (defined in ui_brushgizmo.cpp)
void XORgizmo_Draw(AppState*);
void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Draw(AppState* state, Rectangle vp);

bool quickPanelShow = false;
int quickPanelMouseMode = 0;
bool g_colorPicking = false;
Color g_colorPickGrid[9] = {};

// Sub-component visibility toggles (controlled from left panel)
bool g_showBrushPreview = true;   // gizmo ring + rotation arrow
bool g_showStampPreview = true;   // brush stamp preview (quick panel overlay)
bool g_showTextureGroup = true;   // texture controls (dropdowns, sliders, grid)
bool g_showFilePanel = true;     // file operations panel
bool g_showToolPanel = true;      // tool selection toolbox

// Debug logging - first open flag
static bool g_quickPanelFirstOpen = true;
static bool g_quickPanelTexLogged = false;

void QuickPanel_Init(void) {
    FilePanel_Init();
    ToolBox_Init();
}

void QuickPanel_Shutdown(void) {
    FilePanel_Shutdown();
    ToolBox_Shutdown();
}

void QuickPanel_DrawGizmo(AppState* state) {
    XORgizmo_Draw(state);
}

void QuickPanel_DrawUI(AppState* state) {
    if (!quickPanelShow) return;

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    rlSetBlendMode(RL_BLEND_ALPHA);
    if (g_showFilePanel) FilePanel_Draw(state, vp);
    rlSetBlendMode(RL_BLEND_ALPHA);
    if (g_showToolPanel) ToolBox_Draw(state, vp);

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

    BParam* bps[6] = {&bpOpacity, &bpSpacing, &bpScatter, &bpQuickHue, &bpQuickSat, &bpQuickLit};
    const char* labels[6] = {"Op", "Sp", "Sc", "H", "S", "L"};
    int colorModes[6] = {-1, -1, -1, 0, 1, 2};

    ImVec2 mp = ImGui::GetMousePos();
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

        DrawSliderVertical(dl, bp, colX, slY, dCtrl, (int)dLen, bp->slider.clipmaxF, colorModes[i]);

        ImGui::PushID(400 + i);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
        rlSetBlendMode(RL_BLEND_ALPHA);
        ImGui::InvisibleButton("##sb", ImVec2(dCtrl, (int)dLen),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        if (ImGui::IsItemHovered() && bps[i]->tooltip[0])
            ImGui::SetTooltip("%s", bps[i]->tooltip);
        if (ImGui::IsItemActive()) {
            float t = (mp.y - slY) / (float)dLen;
            t = fminf(1.0f, fmaxf(0.0f, t));
            t = 1.0f - t;
            if (ImGui::IsMouseDown(0))
                bps[i]->slider.clipmaxF = t;
            else if (ImGui::IsMouseDown(1))
                bps[i]->slider.clipminF = t;
            else if (ImGui::IsMouseDown(2))
                bps[i]->slider.jitter = t;
        }
        ImGui::PopID();

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

     if (g_showTextureGroup) {
          int texAreaY = iconY + dCtrl + 14;
         int texCount = state->brushTexCount;

         BParam_SetValue(&bpTexScale, state->currentBrush.Realb.texScale);
         BParam_SetValue(&bpTexFeather, state->currentBrush.Realb.texFeather);
         BParam_SetValue(&bpTexThresh, state->currentBrush.Realb.texThresh);
         BParam_SetValue(&bpTexBlendVal, state->currentBrush.Realb.texBlendVal);

         float sectionWidth = vp.width / 5.0f;
         float baseX = vp.x + sectionWidth;

         rlSetBlendMode(RL_BLEND_ALPHA);
         ImGui::SetCursorScreenPos(ImVec2(baseX, texAreaY));
         if (ImGui::BeginChild("##texCol2", ImVec2(sectionWidth, 0), false)) {
             int mm = state->currentBrush.Realb.useTexLumAsAlpha ? 1 : 0;
             ImGui::Text("Mask Mode");
             if (ImGui::RadioButton("lum is alpha", &mm, 1))
                 state->currentBrush.Realb.useTexLumAsAlpha = true;
             if (ImGui::RadioButton("tex.a is alpha", &mm, 0))
                 state->currentBrush.Realb.useTexLumAsAlpha = false;

         ImGui::Spacing();
         rlSetBlendMode(RL_BLEND_ALPHA);
         int mx = state->currentBrush.Realb.texBlendMode;
         ImGui::Text("Mask Mix");
         if (ImGui::RadioButton("multiply", &mx, 0))
             state->currentBrush.Realb.texBlendMode = 0;
         if (ImGui::RadioButton("threshold", &mx, 1))
             state->currentBrush.Realb.texBlendMode = 1;
         if (ImGui::RadioButton("use tex mask", &mx, 2))
             state->currentBrush.Realb.texBlendMode = 2;

         ImGui::Spacing();
         ImGui::Separator();
         rlSetBlendMode(RL_BLEND_ALPHA);
         int tnm = state->currentBrush.Realb.texNoisemode;
         ImGui::Text("Sample Mode");
         ImGui::SetNextItemWidth(sectionWidth * 0.85f);
         if (ImGui::Combo("##noise", &tnm, "Stencil\0Random\0Const\0"))
             state->currentBrush.Realb.texNoisemode = tnm;
         ImGui::EndChild();
         }

         rlSetBlendMode(RL_BLEND_ALPHA);
         ImGui::SetCursorScreenPos(ImVec2(baseX + sectionWidth, texAreaY));
         if (ImGui::BeginChild("##texCol3", ImVec2(sectionWidth, 0), false)) {
             int cm = state->currentBrush.Realb.texColorMode;
             ImGui::Text("Color");
             if (ImGui::RadioButton("brush RGB", &cm, 0))
                 state->currentBrush.Realb.texColorMode = 0;
             if (ImGui::RadioButton("texture RGB", &cm, 1))
                 state->currentBrush.Realb.texColorMode = 1;
             if (ImGui::RadioButton("mul brush*tex", &cm, 2))
                 state->currentBrush.Realb.texColorMode = 2;

             ImGui::Spacing();
             ImGui::Separator();
             rlSetBlendMode(RL_BLEND_ALPHA);
             DrawBParamSlider(&bpTexScale);
             rlSetBlendMode(RL_BLEND_ALPHA);
             DrawBParamSlider(&bpTexFeather);
             rlSetBlendMode(RL_BLEND_ALPHA);
             DrawBParamSlider(&bpTexThresh);
             rlSetBlendMode(RL_BLEND_ALPHA);
             DrawBParamSlider(&bpTexBlendVal);
             ImGui::EndChild();
         }

         state->currentBrush.Realb.texScale = BParam_GetValue(&bpTexScale);
         state->currentBrush.Realb.texFeather = BParam_GetValue(&bpTexFeather);
         state->currentBrush.Realb.texThresh = BParam_GetValue(&bpTexThresh);
         state->currentBrush.Realb.texBlendVal = BParam_GetValue(&bpTexBlendVal);

          rlSetBlendMode(RL_BLEND_ALPHA);
          float gridX = baseX + 2.0f * sectionWidth;
          ImGui::SetCursorScreenPos(ImVec2(gridX, texAreaY));
          if (ImGui::BeginChild("##texCol4", ImVec2(sectionWidth, 0), false)) {
              ImVec2 childOrigin = ImGui::GetCursorScreenPos();
              int texCols = 4;
              int texSz = 64;
              int texGap = 6;

              ImGui::SetCursorScreenPos(ImVec2(childOrigin.x, childOrigin.y));
              ImGui::PushID("500");
              bool isNone = (state->activeBrushTex < 0);
              if (isNone) { ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1)); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.4f,0.4f,1)); }
              if (ImGui::Button("X", ImVec2(texSz, texSz)))
                  BrushTex_SetActive(state, -1);
              if (isNone) { ImGui::PopStyleColor(2); }
              ImGui::PopID();

              rlSetBlendMode(RL_BLEND_ALPHA);
              for (int ti = 0; ti < texCount && ti < texCols * 4; ti++) {
                  int col = ti % texCols;
                  int row = ti / texCols;
                  float tx = childOrigin.x + col * (texSz + texGap);
                  float ty = childOrigin.y + row * (texSz + texGap) + texSz + texGap;
                  ImGui::SetCursorScreenPos(ImVec2(tx, ty));
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
              ImGui::EndChild();
         }
     }

     ImGui::End();
 }
