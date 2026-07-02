#include "repaint.h"
#include "ui_rect.h"
#include "xform.h"
#include "transform_handle.h"
#include "sd_client.h"
#include "upscale_client.h"
#include "layerstack.h"
#include "imgui.h"
#include "raylib.h"
#include "rlgl.h"
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdio>

#define PX_PER_UNIT 256.0f

// ── Selection state ───────────────────────────────────────────────────
static RectXform  g_sdXform = {};
static Vector2    g_sdCursor = {};

// ── UI controls ───────────────────────────────────────────────────────
static int    g_resolution = 512;
static bool   g_lockSquare = true;
static float  g_strength   = 0.4f;
static float  g_guidance   = 1.3f;
static int    g_steps      = 4;
static char   g_prompt[1024] = "";

// ── Thread state ──────────────────────────────────────────────────────
static std::atomic<bool>  g_running{false};
static std::atomic<bool>  g_threadDone{false};
static std::atomic<bool>  g_threadOk{false};
static uint8_t*           g_resultPng = nullptr;
static size_t             g_resultSize = 0;
static char               g_progressMsg[256] = "";

static void progress_cb(const char* msg) {
    snprintf(g_progressMsg, sizeof(g_progressMsg), "%s", msg);
}

static void sd_worker(char* prompt, float strength, float cfg,
                       int steps, int w, int h) {
    uint8_t* png = nullptr;
    size_t   sz  = 0;
    bool ok = (sd_request(prompt, strength, cfg, steps, w, h,
                          &png, &sz, progress_cb) == 0);
    free(prompt);
    g_resultPng   = png;
    g_resultSize  = sz;
    g_threadOk    = ok;
    g_threadDone  = true;
    g_running     = false;
}

static void upscale_worker(Image src_img) {
    /* Encode source image as PNG using raylib */
    int png_size = 0;
    unsigned char* png_data = ExportImageToMemory(src_img, ".png", &png_size);
    UnloadImage(src_img);
    if (!png_data || png_size == 0) { g_threadDone = true; g_running = false; g_threadOk = false; return; }

    uint8_t* result = nullptr;
    size_t   rsz = 0;
    bool ok = (upscale_request(png_data, (size_t)png_size, &result, &rsz) == 0);
    free(png_data);

    g_resultPng   = result;
    g_resultSize  = rsz;
    g_threadOk    = ok;
    g_threadDone  = true;
    g_running     = false;
}

// ── SDHudModule ───────────────────────────────────────────────────────

// ── SDHudModule ───────────────────────────────────────────────────────

bool SDHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (!ImGui::IsAnyItemActive() && input.KeyPressed(KEY_FOUR)) {
        if (g_activeHud == HUD_SD) {
            HudSetActive(state, HUD_NONE);
        } else {
            g_sdXform = RectXform_Pivot(
                state->camera.target.x,
                state->camera.target.y,
                2.0f, 2.0f, 0);
            g_sdCursor = {state->camera.target.x, state->camera.target.y};
            TransformHandle_ResetState();
            HudSetActive(state, HUD_SD);
        }
        return true;
    }
    if (g_activeHud != HUD_SD) return false;

    // Prevent transform interaction when mouse is over ImGui controls
    if (ImGui::IsAnyItemHovered()) {
        input.mouseCaptured = true;
        return true;
    }

    if (!input.mouseCaptured && rect.Contains(input.MousePos())) {
        bool changed = TransformHandle_Input(&g_sdXform, &g_sdCursor, false,
            g_lockSquare, &state->camera, input.MousePos(),
            input.MouseDown(MOUSE_LEFT_BUTTON),
            input.MousePressed(MOUSE_LEFT_BUTTON),
            input.MouseDown(MOUSE_RIGHT_BUTTON),
            input.MousePressed(MOUSE_RIGHT_BUTTON),
            &rect);
        if (changed) {
            input.mouseCaptured = true;
        }
    }

    if (input.KeyPressed(KEY_ESCAPE)) {
        HudSetActive(state, HUD_NONE);
    }
    return false;
}

void SDHudModule::DrawGL(const DrawRect& rect) {
    if (g_activeHud != HUD_SD) return;
    TransformHandle_Draw(&g_sdXform, g_sdCursor, &state->camera);
}

void SDHudModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_SD) return;

    float rx = rect.x, ry = rect.y, rw = rect.w, rh = rect.h;
    float margin = 10.0f;
    float thirdW = (rw - margin * 4) / 3.0f;
    float bottomRowH = 200.0f;
    float previewW = 220.0f, previewH = 220.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

    // ── Preview (top-left) ────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(rx + margin, ry + margin));
    ImGui::SetNextWindowSize(ImVec2(previewW, previewH));
    ImGui::Begin("##sdPrev", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    {
        ImGui::Text("Preview  %d x %d", g_resolution, g_resolution);
        ImGui::Separator();
        if (g_resultPng && g_resultSize > 0) {
            Image img = LoadImageFromMemory(".png", g_resultPng, g_resultSize);
            if (img.data) {
                float scale = fminf((previewW - 20) / img.width,
                                    (previewH - 40) / img.height);
                int rw_img = (int)(img.width * scale);
                int rh_img = (int)(img.height * scale);
                ImageResize(&img, rw_img, rh_img);
                Texture2D tex = LoadTextureFromImage(img);
                UnloadImage(img);
                ImGui::Image((ImTextureID)(intptr_t)tex.id,
                             ImVec2((float)rw_img, (float)rh_img));
                UnloadTexture(tex);
            }
        } else {
            ImGui::TextUnformatted(g_running ? "Generating..." : "Set prompt + Generate");
        }
    }
    ImGui::End();

    // ── Bottom row: 3 panels ─────────────────────────────────────────
    float by = ry + rh - bottomRowH - margin;

    // Sliders
    ImGui::SetNextWindowPos(ImVec2(rx + margin, by));
    ImGui::SetNextWindowSizeConstraints(ImVec2(thirdW, 0), ImVec2(thirdW, bottomRowH));
    ImGui::Begin("##sdSlid", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);
    {
        if (g_lockSquare) g_resolution = 512;
        DrawSingleSliderInt("Resolution", &g_resolution, 16, 2048, "Resolution %d px");
        g_resolution = (g_resolution / 8) * 8;

        DrawSingleSlider("Strength", &g_strength, 0.0f, 1.0f, "Strength %.2f");

        DrawSingleSlider("Guidance", &g_guidance, 0.0f, 2.0f, "Guidance %.2f");

        int s = g_steps;
        DrawSingleSliderInt("Steps", &s, 2, 16, "Steps %d");
        s = (s / 2) * 2; if (s < 2) s = 2;
        g_steps = s;
    }
    ImGui::End();

    // Prompt
    ImGui::SetNextWindowPos(ImVec2(rx + margin * 2 + thirdW, by));
    ImGui::SetNextWindowSize(ImVec2(thirdW, bottomRowH));
    ImGui::Begin("##sdProm", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("Prompt");
        ImGui::InputTextMultiline("##pr", g_prompt, sizeof(g_prompt),
            ImVec2(thirdW - 20, bottomRowH - 36));
    }
    ImGui::End();

    // Controls
    ImGui::SetNextWindowPos(ImVec2(rx + margin * 3 + thirdW * 2, by));
    ImGui::SetNextWindowSizeConstraints(ImVec2(thirdW, 0), ImVec2(thirdW, bottomRowH));
    ImGui::Begin("##sdCtrl", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::BeginGroup();
        ImGui::Checkbox("512x512", &g_lockSquare);
        int outH = g_lockSquare ? g_resolution
                  : (int)(g_resolution * (g_sdXform.h / g_sdXform.w) + 0.5f);
        ImGui::Text("Output: %d x %d", g_resolution, outH);
        ImGui::Separator();
        bool canUpscale = !g_running && state->activeLayer >= 0;
        ImGui::BeginDisabled(!canUpscale);
        if (ImGui::Button("Upscale", ImVec2(-1, 28))) {
            int pw = g_resolution;
            int ph = g_lockSquare ? pw
                     : (int)(pw * (g_sdXform.h / g_sdXform.w) + 0.5f);
            if (pw < 16) pw = 16;
            if (ph < 16) ph = 16;

            /* Set up 2D camera to map xform world rect to the temp RT */
            Rectangle wb = GetWorldAABB(&g_sdXform);
            Camera2D cam;
            cam.target = {wb.x + wb.width * 0.5f, wb.y + wb.height * 0.5f};
            cam.offset = {(float)pw * 0.5f, (float)ph * 0.5f};
            cam.rotation = 0;
            cam.zoom = (float)pw / wb.width;

            RenderTexture2D rt = LoadRenderTexture(pw, ph);
            BeginTextureMode(rt);
            ClearBackground(BLANK);
            BeginMode2D(cam);
            for (int i = 0; i < LayerStack_Count(); i++) {
                sLayerProps* lp = LayerStack_GetProps(i);
                if (!lp->visible) continue;
                RenderTexture2D lrt = LayerStack_GetRT(i);
                Rectangle src = {0, 0, (float)lp->layerW, (float)-lp->layerH};
                float dx = lp->xform.mat[2];
                float dy = lp->xform.mat[5];
                DrawTexturePro(lrt.texture, src,
                    {dx, dy, (float)lp->layerW, (float)lp->layerH}, {0,0}, 0, WHITE);
            }
            EndMode2D();
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            UnloadRenderTexture(rt);
            ImageFlipVertical(&img);

            if (img.data) {
                g_running = true;
                g_threadDone = false;
                g_progressMsg[0] = '\0';
                std::thread t(upscale_worker, img);
                t.detach();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();

        float availY = ImGui::GetContentRegionAvail().y;
        ImGui::Dummy(ImVec2(0, fmaxf(availY - 36, 4)));

        bool canGen = g_prompt[0] && !g_running;
        ImGui::BeginDisabled(!canGen);
        if (ImGui::Button("Generate", ImVec2(-1, 32))) {
            int pw = g_resolution;
            int ph = g_lockSquare ? pw
                     : (int)(pw * (g_sdXform.h / g_sdXform.w) + 0.5f);
            if (ph < 16) ph = 16;
            char* pc = strdup(g_prompt);
            g_running = true;
            g_threadDone = false;
            g_progressMsg[0] = '\0';
            std::thread t(sd_worker, pc, g_strength, g_guidance, g_steps, pw, ph);
            t.detach();
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted(g_progressMsg);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);

    // ── Poll thread result ───────────────────────────────────────────
    if (g_threadDone) {
        if (g_threadOk && g_resultPng && g_resultSize > 0) {
            Image img = LoadImageFromMemory(".png", g_resultPng, g_resultSize);
            if (img.data) {
                int n = LayerStack_Count();
                int newIdx = LayerStack_InsertLayer(n > 0 ? n - 1 : 0);
                ImageFlipVertical(&img);
                Texture2D tmp = LoadTextureFromImage(img);
                UnloadImage(img);
                BeginTextureMode(LayerStack_GetRT(newIdx));
                ClearBackground(BLANK);
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                DrawTexture(tmp, 0, 0, WHITE);
                rlSetBlendMode(RL_BLEND_ALPHA);
                EndTextureMode();
                UnloadTexture(tmp);
                state->activeLayer = newIdx;
                layersDirty = true;
            }
        }
        free(g_resultPng);
        g_resultPng = nullptr;
        g_resultSize = 0;
        g_threadDone = false;
        g_threadOk = false;
    }
}
