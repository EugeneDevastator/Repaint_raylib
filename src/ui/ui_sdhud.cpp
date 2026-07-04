#include "repaint.h"
#include "ui_rect.h"
#include "xform.h"
#include "transform_handle.h"
#include "viewport_manager.h"
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

// ── Input preview (composited layers inside g_sdXform area) ───────────
static RenderTexture2D g_inputPreview = {0};
static bool            g_previewDirty = true;
static bool            g_hoverPreview = false;

// Build viewXform: maps g_sdXform world-rect to output of (pw, ph) pixels.
static RectXform BuildCropViewXform(int pw, int ph) {
    RectXform vx = {};
    if (g_sdXform.ww > 0.0f && g_sdXform.wh > 0.0f) {
        float su = pw / g_sdXform.ww;
        float sv = ph / g_sdXform.wh;
        float* m = g_sdXform.mat;
        float det = m[0]*m[4] - m[1]*m[3];
        if (fabsf(det) > 0.0001f) {
            float invDet = 1.0f/det;
            float ia = m[4]*invDet, ib = -m[1]*invDet;
            float ic = -m[3]*invDet, id = m[0]*invDet;
            float itx = (m[1]*m[5] - m[4]*m[2])*invDet;
            float ity = (m[3]*m[2] - m[0]*m[5])*invDet;
            vx.mat[0]=ia*su; vx.mat[1]=ib*su; vx.mat[2]=itx*su;
            vx.mat[3]=ic*sv; vx.mat[4]=id*sv; vx.mat[5]=ity*sv;
        }
    }
    return vx;
}

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
            g_previewDirty = true;
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

    // Hover overlay: show composited input at g_sdXform world rect with point filtering
    if (g_hoverPreview && g_inputPreview.id) {
        Rectangle aabb = GetWorldAABB(&g_sdXform);
        if (aabb.width > 0 && aabb.height > 0) {
            SetTextureFilter(g_inputPreview.texture, TEXTURE_FILTER_POINT);
            rlDrawRenderBatchActive();
            DrawTexturePro(g_inputPreview.texture,
                Rectangle{0,0,(float)g_inputPreview.texture.width,(float)g_inputPreview.texture.height},
                aabb, Vector2{0,0}, 0, ColorAlpha(WHITE, 0.85f));
            SetTextureFilter(g_inputPreview.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
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
        ImGui::Text("Preview");
        ImGui::Separator();
        int pw = g_resolution;
        int ph = g_lockSquare ? pw
                 : (int)(pw * (g_sdXform.wh / g_sdXform.ww) + 0.5f);
        if (ph < 16) ph = 16;
        pw = (pw / 8) * 8; ph = (ph / 8) * 8;

        // Update cached composite when dirty
        if (g_previewDirty || g_inputPreview.id == 0 ||
            g_inputPreview.texture.width != (unsigned int)pw ||
            g_inputPreview.texture.height != (unsigned int)ph)
        {
            if (g_inputPreview.id) UnloadRenderTexture(g_inputPreview);
            g_inputPreview = LoadRenderTexture(pw, ph);
            RectXform viewXf = BuildCropViewXform(pw, ph);
            ViewportManager_CompositeViewInto(g_inputPreview, &viewXf, pw, ph);
            g_previewDirty = false;
        }

        if (g_inputPreview.id) {
            float scale = fminf((previewW - 20) / pw, (previewH - 40) / ph);
            int dw = (int)(pw * scale), dh = (int)(ph * scale);
            if (dw < 1) dw = 1; if (dh < 1) dh = 1;
            SetTextureFilter(g_inputPreview.texture, TEXTURE_FILTER_BILINEAR);
            ImGui::Image((ImTextureID)(intptr_t)g_inputPreview.texture.id,
                         ImVec2((float)dw, (float)dh));
            g_hoverPreview = ImGui::IsItemHovered();
            ImGui::SameLine();
            ImGui::Text("%d x %d", pw, ph);
        } else {
            ImGui::Text("No layers");
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
                  : (int)(g_resolution * (g_sdXform.wh / g_sdXform.ww) + 0.5f);
        ImGui::Text("Output: %d x %d", g_resolution, outH);
        ImGui::Separator();
        bool canUpscale = !g_running && state->activeLayer >= 0;
        ImGui::BeginDisabled(!canUpscale);
        if (ImGui::Button("Upscale", ImVec2(-1, 28))) {
            int pw = g_resolution;
            int ph = g_lockSquare ? pw
                     : (int)(pw * (g_sdXform.wh / g_sdXform.ww) + 0.5f);
            if (pw < 16) pw = 16;
            if (ph < 16) ph = 16;

            RenderTexture2D rt = LoadRenderTexture(pw, ph);
            RectXform viewXf = BuildCropViewXform(pw, ph);
            ViewportManager_CompositeViewInto(rt, &viewXf, pw, ph);
            Image img = LoadImageFromTexture(rt.texture);
            UnloadRenderTexture(rt);

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
                     : (int)(pw * (g_sdXform.wh / g_sdXform.ww) + 0.5f);
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
                int newIdx = ViewportManager_CreateLayerFromImage(img);
                if (newIdx >= 0) {
                    state->activeLayer = newIdx;
                    layersDirty = true;
                }
            }
        }
        free(g_resultPng);
        g_resultPng = nullptr;
        g_resultSize = 0;
        g_threadDone = false;
        g_threadOk = false;
    }
}
