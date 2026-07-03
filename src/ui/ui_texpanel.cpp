#include "repaint.h"
#include "brush_preset.h"
#include "ui_texpanel.h"
#include "texture_manager.h"
#include "rlgl.h"
#include "external/glad.h"
#include "imgui.h"
#include <math.h>
#include <algorithm>

static int g_texPanelDragMode = 0;

void TexPanelModule::DrawGL(const DrawRect& rect) {
    if (g_activeHud != HUD_QUICK) return;
    if (g_texPanelAreaY <= 0) return;

    // Check if active brush slot is valid — scan bucket 0 to see if slot still exists
    TexSlotID activeId = state->brushTexSlot;
    TexSlot* activeTs = TM_Get(activeId);

    float gap = 4.0f;
    float pW = (rect.w - gap * 2.0f) / 3.0f;
    float bgY = (float)g_texPanelAreaY;
    float bgH = rect.y + rect.h - bgY;

    // ── Background rect for selectors + sliders + preview area ──
    DrawRectangleRec(Rectangle{rect.x, bgY, pW * 2.0f, bgH}, Color{245, 245, 245, 255});

    // ── Texture preview (1.2× bigger) ──
    float pvSz = 230.0f;
    float pvX = rect.x + pW + gap + (pW - pvSz) * 0.5f;
    float pvY = bgY + 135.0f;

    if (activeTs && activeTs->rt.id > 0) {
        float texW = (float)activeTs->w, texH = (float)activeTs->h;
        float scale = fminf(pvSz / texW, pvSz / texH);
        float dw = texW * scale, dh = texH * scale;
        float dx = pvX + (pvSz - dw) * 0.5f;
        float dy = pvY + (pvSz - dh) * 0.5f;
        DrawTexturePro(activeTs->rt.texture,
            Rectangle{0, 0, texW, -texH},
            Rectangle{dx, dy, dw, dh},
            Vector2{0, 0}, 0.0f, WHITE);
    }

    // ── XOR handles ─────────────────────────────────────────────────
    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    float ox = pvX + state->currentBrush.Realb.userTexOriginX * pvSz;
    float oy = pvY + state->currentBrush.Realb.userTexOriginY * pvSz;
    float angle = state->currentBrush.Realb.userTexDirection;
    float arrowLen = 40.0f;

    DrawCircleLines((int)ox, (int)oy, 6.0f, WHITE);
    DrawCircle((int)ox, (int)oy, 2.0f, WHITE);

    float ax = ox + cosf(angle) * arrowLen;
    float ay = oy + sinf(angle) * arrowLen;
    DrawLineEx(Vector2{ox, oy}, Vector2{ax, ay}, 2.0f, WHITE);
    float ah = 0.4f, ahLen = 10.0f;
    DrawLineEx(Vector2{ax, ay},
        Vector2{ax + cosf(angle + (float)M_PI + ah) * ahLen,
                ay + sinf(angle + (float)M_PI + ah) * ahLen}, 2.0f, WHITE);
    DrawLineEx(Vector2{ax, ay},
        Vector2{ax + cosf(angle + (float)M_PI - ah) * ahLen,
                ay + sinf(angle + (float)M_PI - ah) * ahLen}, 2.0f, WHITE);

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);

    // ── Input ───────────────────────────────────────────────────────
    Vector2 mp = GetMousePosition();
    bool over = (mp.x >= pvX && mp.x <= pvX + pvSz &&
                 mp.y >= pvY && mp.y <= pvY + pvSz);
    if (over && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        float dc = sqrtf((mp.x - ox) * (mp.x - ox) + (mp.y - oy) * (mp.y - oy));
        if (dc <= 12.0f) {
            g_texPanelDragMode = 1; // drag origin circle
        } else {
            float da = sqrtf((mp.x - ax) * (mp.x - ax) + (mp.y - ay) * (mp.y - ay));
            if (da <= 10.0f) {
                g_texPanelDragMode = 2; // drag arrow tip
            } else {
                // Clicked elsewhere — set pivot immediately, then drag
                state->currentBrush.Realb.userTexOriginX =
                    fminf(fmaxf((mp.x - pvX) / pvSz, 0.0f), 1.0f);
                state->currentBrush.Realb.userTexOriginY =
                    fminf(fmaxf((mp.y - pvY) / pvSz, 0.0f), 1.0f);
                g_texPanelDragMode = 1;
            }
        }
    }
    if (g_texPanelDragMode == 1 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        state->currentBrush.Realb.userTexOriginX =
            fminf(fmaxf((mp.x - pvX) / pvSz, 0.0f), 1.0f);
        state->currentBrush.Realb.userTexOriginY =
            fminf(fmaxf((mp.y - pvY) / pvSz, 0.0f), 1.0f);
    }
    if (g_texPanelDragMode == 2 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        state->currentBrush.Realb.userTexDirection =
            atan2f(mp.y - oy, mp.x - ox);
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        g_texPanelDragMode = 0;
}

bool TexPanelModule::HandleInput(InputState& input, const DrawRect& rect) {
    (void)rect;
    if (input.mouseCaptured) return false;
    if (g_activeHud != HUD_QUICK) return false;
    if (g_texPanelAreaY > 0 && input.MousePos().y >= g_texPanelAreaY) {
        input.mouseCaptured = true;
        return true;
    }
    return false;
}

void TexPanelModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_QUICK) return;
    if (g_texPanelAreaY <= 0) return;

    rlSetBlendMode(RL_BLEND_ALPHA);

    float thirdW = rect.w / 3.0f;
    float yPos = (float)g_texPanelAreaY;

    // ── Three-column layout with light gray backgrounds (matching layer panel) ──
    int mm = state->currentBrush.Realb.useTexLumAsAlpha ? 0 : 1;
    int tnm = state->currentBrush.Realb.texNoisemode;
    int cm = state->currentBrush.Realb.texColorMode;

    static const char* items0[] = {"lum is alpha", "tex.a is alpha"};
    static const char* items2[] = {"Stencil", "Random", "Const"};
    static const char* items3[] = {"brush RGB", "texture RGB", "mul brush*tex", "lum-color"};
    static const char* items4[] = {"Brush", "Global"};

    float gap = 4.0f;
    float pW = (rect.w - gap * 2.0f) / 3.0f;
    ImVec4 bg(0.96f, 0.96f, 0.96f, 1.0f);

    rlSetBlendMode(RL_BLEND_ALPHA);

    // ── Left area: selectors in a transparent child (constrained to 1/3) ──
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::SetCursorScreenPos(ImVec2(rect.x + 6, yPos + 6));
    ImGui::BeginChild("##texLeft", ImVec2(pW - 12.0f, 0), false);
    {
        float sw = (ImGui::GetContentRegionAvail().x - 4.0f) / 3.0f;

        ImGui::BeginChild("##h0", ImVec2(sw, 0), false);
        DrawSelector("Mask Mode", &mm, items0, 2);
        ImGui::EndChild();
        ImGui::SameLine(0, 2);

        ImGui::BeginChild("##h1", ImVec2(sw, 0), false);
        DrawSelector("Color", &cm, items3, 4);
        ImGui::EndChild();
        ImGui::SameLine(0, 2);

        ImGui::BeginChild("##h2", ImVec2(sw, 0), false);
        DrawSelector("Sample Mode", &tnm, items2, 3);
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(0, 150));
        DrawSelector("Tex Scale", &g_texScaleMode, items4, 2);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ── Center area: texture sliders above preview, transparent child ──
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::SetCursorScreenPos(ImVec2(rect.x + pW + gap + 6, yPos + 6));
    ImGui::BeginChild("##texCtr", ImVec2(pW - 12.0f, 0), false);
    {
        DrawSlider(&bpTexScale, 0);
        rlSetBlendMode(RL_BLEND_ALPHA);
        DrawSlider(&bpTexFeather, 0);
        rlSetBlendMode(RL_BLEND_ALPHA);
        DrawSlider(&bpTexThresh, 0);
        rlSetBlendMode(RL_BLEND_ALPHA);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ── Right panel: texture grid (1/3 width) ──
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::SetCursorScreenPos(ImVec2(rect.x + (pW + gap) * 2.0f, yPos));
    ImGui::BeginChild("##texGridPanel", ImVec2(pW, 0), ImGuiChildFlags_Borders);
    float gridH = ImGui::GetContentRegionAvail().y;
    if (gridH < 20) gridH = 20;
    int texSz = 72, texGap = 1;
    int texCols = (int)(ImGui::GetContentRegionAvail().x) / (texSz + texGap);
    if (texCols < 2) texCols = 2;
    ImGui::BeginChild("##texGrid", ImVec2(0, gridH), ImGuiChildFlags_Borders);
    {
    // "No Tex" button
    ImGui::PushID("texNone");
    bool isNoTex = !TM_IsValid(state->brushTexSlot);
    if (isNoTex) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1));
    if (ImGui::ImageButton("##tn", (ImTextureID)0, ImVec2(texSz, texSz))) {
        state->brushTexSlot = TM_INVALID_SLOT;
    }
    if (isNoTex) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("No Texture");
    ImGui::PopID();

    // All textures in one grid (no built-in/user split)
    int col = 0;
    for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++) {
        TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
        TexSlot* ts = TM_Get(id);
        if (!ts) continue;
        if (col % texCols != 0) ImGui::SameLine(0, texGap);
        col++; ImGui::PushID(700 + s);
        Texture2D thumb = BrushTex_GetThumb(state, id);
        bool isSel = TM_IsValid(state->brushTexSlot) && state->brushTexSlot == id;
        if (isSel) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
        if (thumb.id > 0 && ImGui::ImageButton("##t", (ImTextureID)(intptr_t)thumb.id, ImVec2(texSz, texSz), ImVec2(0,1), ImVec2(1,0))) {
            state->brushTexSlot = id;
        }
        if (isSel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ts->name);
        ImGui::PopID();
    }
    }
    ImGui::EndChild();

    state->currentBrush.Realb.useTexLumAsAlpha = (mm == 0);
    state->currentBrush.Realb.texNoisemode = tnm;
    state->currentBrush.Realb.texColorMode = cm;

    ImGui::PopStyleColor();
    ImGui::EndChild();
}
