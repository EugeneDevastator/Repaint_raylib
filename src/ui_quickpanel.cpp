#include "repaint.h"
#include "imgui.h"
#include <math.h>

// Sub-component declarations (defined in their own files)
void BrushGizmo_Draw(ImDrawList* dl, ImVec2 org, int gcx, int gcy, AppState*);
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
bool g_showFilePanel = false;     // file operations panel (default off)
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

void QuickPanel_Draw(AppState* state) {
    if (!quickPanelShow) return;

    // ── Debug logging on first open ──────────────────────────────
    if (g_quickPanelFirstOpen) {
        g_quickPanelFirstOpen = false;
        TraceLog(LOG_INFO, "[QuickPanel] First open — logging state");
        TraceLog(LOG_INFO, "[QuickPanel] brushTexCount=%d, activeBrushTex=%d, editTexMode=%d",
            state->brushTexCount, state->activeBrushTex, state->editTexMode);
        TraceLog(LOG_INFO, "[QuickPanel] g_showBrushPreview=%d g_showStampPreview=%d g_showTextureGroup=%d g_showFilePanel=%d g_showToolPanel=%d",
            (int)g_showBrushPreview, (int)g_showStampPreview, (int)g_showTextureGroup,
            (int)g_showFilePanel, (int)g_showToolPanel);
    }

    Rectangle vp = viewport.bounds;
    int gcx = (int)(vp.x + vp.width * 0.5f);
    int gcy = (int)(vp.y + vp.height * 0.5f);

    // Use base slider values (not velocity-modulated) for gizmo display
    float baseRadOut = BParam_GetValue(&bpSize);
    float baseHard   = BParam_GetValue(&bpHardness);
    float drawRadOut = baseRadOut * state->camera.zoom;
    float drawRadIn  = baseRadOut * baseHard * state->camera.zoom;
    if (drawRadIn > drawRadOut) drawRadIn = drawRadOut;

    // ── Separate panels (top-level windows, drawn before overlay) ────────
    if (g_showFilePanel) FilePanel_Draw(state, vp);
    if (g_showToolPanel) ToolBox_Draw(state, vp);

    // ── Transparent overlay for gizmo visuals + interactive controls ─────
    ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
    ImGui::SetNextWindowSize(ImVec2(vp.width, vp.height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##quickpanel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 org((float)gcx, (float)gcy);
    int gizR = 200;
    float d30 = (float)(M_PI * 30.0 / 180.0);

    if (g_showBrushPreview) BrushGizmo_Draw(dl, org, gcx, gcy, state);

    // ── Radial input handling (sector-based, no distance limit) ─────────
    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - gcx, dy = mp.y - gcy;
    float dist = sqrtf(dx * dx + dy * dy);
    float ang = AtanXY(dx, dy);

    bool down = ImGui::IsMouseDown(0);
    bool clicked = ImGui::IsMouseClicked(0);
    bool released = ImGui::IsMouseReleased(0);

    if (clicked && !ImGui::IsAnyItemHovered()) {
        // Arrow proximity check (takes priority over sector modes)
        float arrowAng = state->initialAngle * (float)(M_PI * 2.0 / 360.0);
        float angDiff = fabsf(ang - arrowAng);
        if (angDiff > (float)M_PI) angDiff = (float)(2.0f * M_PI) - angDiff;
        bool nearArrow = (angDiff < 20.0f * (float)M_PI / 180.0f);

        if (nearArrow) {
            quickPanelMouseMode = 4;
        } else {
            // Sector-based mode detection
            bool inSector1 = (ang > d30 && ang < d30 * 5);       // size sector
            bool inSector2 = (ang < d30 && ang > -(float)M_PI * 0.5f);  // hardness sector
            bool inSector3 = !inSector1 && !inSector2;            // curve sector

            if (dist <= GIZMO_FIXED_RADIUS_PX) {
                // Inside fixed radius: hardness, curve, or size
                if (inSector1) quickPanelMouseMode = 1;
                else if (inSector2) quickPanelMouseMode = 2;
                else quickPanelMouseMode = 3;
            } else {
                // Outside fixed radius: size in sector 1, otherwise rotation
                quickPanelMouseMode = inSector1 ? 1 : 4;
            }
        }
    }

    if (quickPanelMouseMode > 0 && down) {
        float rad = dist / state->camera.zoom;
        float absrad = dist;
        int curMode = 0;
        if (ang > d30 && ang < d30 * 5) curMode = 1;
        else if (ang < d30 && ang > -(float)M_PI * 0.5f) curMode = 2;
        else curMode = 3;

        if (quickPanelMouseMode == 1) {
            float rel = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 1;
            if (curMode != 1) rad = roundf(rad / 10.0f) * 10.0f;
            float newRad = fmaxf(1.0f, rad);
            state->currentBrush.Realb.rad_out = newRad;
            state->currentBrush.Realb.rad_in = newRad * rel;
            if (state->currentBrush.Realb.rad_in > newRad * 0.98f)
                state->currentBrush.Realb.rad_in = newRad * 0.98f;
            BParam_SetValue(&bpSize, newRad);
            float h = (state->currentBrush.Realb.rad_out > 0)
                ? (state->currentBrush.Realb.rad_in / state->currentBrush.Realb.rad_out) : 0;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 2) {
            // Hardness: captured inside radius, but tracks continuously once active
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 2) h = 0.0f;
            if (h < 0.05f) h = 0.0f;
            state->currentBrush.Realb.rad_in = h * state->currentBrush.Realb.rad_out;
            if (state->currentBrush.Realb.rad_in > state->currentBrush.Realb.rad_out * 0.98f)
                state->currentBrush.Realb.rad_in = state->currentBrush.Realb.rad_out * 0.98f;
            BParam_SetValue(&bpHardness, h);
        }
        if (quickPanelMouseMode == 3) {
            // Curve: captured inside radius, but tracks continuously once active
            float h = fminf(absrad / GIZMO_FIXED_RADIUS_PX, 1.0f);
            if (curMode != 3) h = 0.0f;
            state->currentBrush.Realb.crv = 1.0f - h;
            BParam_SetValue(&bpCurvature, 1.0f - h);
        }
        if (quickPanelMouseMode == 4) {
            float newAng = (ang + (float)M_PI) * 180.0f / (float)M_PI;
            if (absrad > 160.0f || rad < 20.0f)
                newAng = roundf(newAng / 22.5f) * 22.5f;
            state->initialAngle = newAng;
        }
    }

    if (released) quickPanelMouseMode = 0;

    // ── Slider columns (dynamic sizes proportional to viewport) ────────
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float dThick = fmaxf(20.0f, fminf(sw / 12.0f, 48.0f));   // 1/12 screen width
    float dLen   = fmaxf(100.0f, fminf(sh / 3.0f, 400.0f));  // 1/3 screen height
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

    for (int i = 0; i < 6; i++) {
        int colX = (i < 3)
            ? sliderLeftX + i * (dCtrl + dGap)
            : sliderRightX + (i - 3) * (dCtrl + dGap);
        BParam* bp = bps[i];

        dl->AddText(ImVec2(colX + dCtrl / 2 - 6, penBtnY - 14), IM_COL32(211, 211, 211, 230), labels[i]);

        // Pen mode button
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

        // Slider body
        DrawSliderVertical(dl, bp, colX, slY, dCtrl, (int)dLen, bp->slider.clipmaxF, colorModes[i]);

        // Invisible button — per-item activation via ImGui button flags
        ImGui::PushID(400 + i);
        ImGui::SetCursorScreenPos(ImVec2(colX, slY));
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

        // Icon at bottom
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

     // ── Brush texture selection + params ──────────────────────────────
     if (g_showTextureGroup) {
          int texAreaY = iconY + dCtrl + 14;
         int texCount = state->brushTexCount;

         // ── Texture controls (3 columns within 1/5 viewport width each) ─────
         BParam_SetValue(&bpTexScale, state->currentBrush.Realb.texScale);
         BParam_SetValue(&bpTexFeather, state->currentBrush.Realb.texFeather);
         BParam_SetValue(&bpTexThresh, state->currentBrush.Realb.texThresh);
         BParam_SetValue(&bpTexBlendVal, state->currentBrush.Realb.texBlendVal);

         float sectionWidth = vp.width / 5.0f;
         float baseX = vp.x + sectionWidth; // start of column 2
         float childHeight = 200.0f; // Fixed height for all columns

         // Column 2: maskmode / maskmix radio buttons
         ImGui::SetCursorScreenPos(ImVec2(baseX, texAreaY));
         if (ImGui::BeginChild("##texCol2", ImVec2(sectionWidth, childHeight), false)) {
             int mm = state->currentBrush.Realb.useTexLumAsAlpha ? 1 : 0;
             ImGui::Text("Mask Mode");
             if (ImGui::RadioButton("lum is alpha", &mm, 1))
                 state->currentBrush.Realb.useTexLumAsAlpha = true;
             if (ImGui::RadioButton("tex.a is alpha", &mm, 0))
                 state->currentBrush.Realb.useTexLumAsAlpha = false;

             ImGui::Spacing();
             int mx = state->currentBrush.Realb.texBlendMode;
             ImGui::Text("Mask Mix");
             if (ImGui::RadioButton("multiply", &mx, 0))
                 state->currentBrush.Realb.texBlendMode = 0;
             if (ImGui::RadioButton("threshold", &mx, 1))
                 state->currentBrush.Realb.texBlendMode = 1;
             if (ImGui::RadioButton("use tex mask", &mx, 2))
                 state->currentBrush.Realb.texBlendMode = 2;
             ImGui::EndChild();
         }

         // Column 3: param sliders
         ImGui::SetCursorScreenPos(ImVec2(baseX + sectionWidth, texAreaY));
         if (ImGui::BeginChild("##texCol3", ImVec2(sectionWidth, childHeight), false)) {
             DrawBParamSlider(&bpTexScale);
             DrawBParamSlider(&bpTexFeather);
             DrawBParamSlider(&bpTexThresh);
             DrawBParamSlider(&bpTexBlendVal);
             ImGui::EndChild();
         }

         // Sync brush state back from BParams
         state->currentBrush.Realb.texScale = BParam_GetValue(&bpTexScale);
         state->currentBrush.Realb.texFeather = BParam_GetValue(&bpTexFeather);
         state->currentBrush.Realb.texThresh = BParam_GetValue(&bpTexThresh);
         state->currentBrush.Realb.texBlendVal = BParam_GetValue(&bpTexBlendVal);

          // Column 4: texture selection grid
          float gridX = baseX + 2.0f * sectionWidth; // start of column 4
          ImGui::SetCursorScreenPos(ImVec2(gridX, texAreaY));
          if (ImGui::BeginChild("##texCol4", ImVec2(sectionWidth, childHeight), false)) {
              ImVec2 childOrigin = ImGui::GetCursorScreenPos();
              int texCols = 4;
              int texSz = 64;
              int texGap = 6;

              // "X" button: no texture used as brush pattern
              ImGui::SetCursorScreenPos(ImVec2(childOrigin.x, childOrigin.y));
              ImGui::PushID("500");
              bool isNone = (state->activeBrushTex < 0);
              if (isNone) { ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1)); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.4f,0.4f,1)); }
              if (ImGui::Button("X", ImVec2(texSz, texSz)))
                  BrushTex_SetActive(state, -1);
              if (isNone) { ImGui::PopStyleColor(2); }
              ImGui::PopID();

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
