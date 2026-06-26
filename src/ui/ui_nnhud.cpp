#include "repaint.h"
#include "layerstack.h"
#include "viewport_manager.h"
#include "matte_client.h"
#include "render_utils.h"
#include "rlgl.h"
#include "imgui.h"
#include <thread>
#include <atomic>
#include <cstdlib>
#include <string.h>

extern bool layersDirty;

// ── Half-float helpers (raylib's FloatToHalf is static in rtextures.c) ─
static uint16_t FloatToHalf(float x) {
    uint32_t i;
    memcpy(&i, &x, sizeof(i));
    uint16_t sign = (i >> 16) & 0x8000;
    int32_t exp = ((i >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = i & 0x007FFFFF;
    if (exp >= 31) return sign | 0x7C00;
    if (exp <= 0) { mant = (mant | 0x00800000) >> (1 - exp); exp = 0; }
    mant = (mant + 0x00001000) >> 13;
    return sign | (uint16_t)(exp << 10) | (uint16_t)mant;
}

// ── Static state ─────────────────────────────────────────────────────
static RenderTexture2D g_resultRT  = {0};
static bool            g_hasResult = false;
static bool            g_processing = false;
static std::thread*    g_workThread = nullptr;
static std::atomic<bool>  g_threadDone{false};
static std::atomic<bool>  g_threadSuccess{false};
static std::atomic<bool> g_progressUpdated{false};
static char               g_progressMsg[256] = "";
static uint8_t*        g_threadAlphaPng  = nullptr;
static size_t          g_threadAlphaSize = 0;
static Image           g_sourceRGB       = {0};
static int             g_srcLayerIdx     = -1;
static char            g_statusText[256] = "";

struct MatteWork {
    uint8_t* rgbPng; size_t rgbSize;
    uint8_t* triPng; size_t triSize;
};

static void MatteProgressCb(const char* msg) {
    strncpy(g_progressMsg, msg, sizeof(g_progressMsg) - 1);
    g_progressMsg[sizeof(g_progressMsg) - 1] = '\0';
    g_progressUpdated.store(true, std::memory_order_release);
}

static void MatteWorker(MatteWork work) {
    uint8_t* outPng = nullptr;
    size_t outSize = 0;
    int ret = matte_request(work.rgbPng, work.rgbSize, work.triPng, work.triSize,
                            &outPng, &outSize, MatteProgressCb);
    if (ret == 0) {
        g_threadAlphaPng  = outPng;
        g_threadAlphaSize = outSize;
    }
    g_threadSuccess = (ret == 0);
    g_threadDone    = true;
    MemFree(work.rgbPng);
    MemFree(work.triPng);
}

// Poll progress from worker thread and update status text.
static void PollProgress(void) {
    if (g_progressUpdated.load(std::memory_order_acquire)) {
        strncpy(g_statusText, g_progressMsg, sizeof(g_statusText) - 1);
        g_progressUpdated.store(false, std::memory_order_relaxed);
    }
}

// Frees thread handle + alpha PNG, but preserves g_sourceRGB for Accept.
static void CleanupThreadOnly(void) {
    if (g_workThread) {
        if (!g_threadDone) g_workThread->detach();
        else g_workThread->join();
        delete g_workThread;
        g_workThread = nullptr;
    }
    if (g_threadAlphaPng) { MemFree(g_threadAlphaPng); g_threadAlphaPng = nullptr; }
    g_threadAlphaSize = 0;
    g_threadDone      = false;
    g_threadSuccess   = false;
}

static void CancelWorkThread(void) {
    if (g_workThread) {
        // Thread has already been launched; we can't cancel the socket op.
        // If the thread is still running, detach it (it will complete in
        // the background and the result will be ignored).
        if (!g_threadDone) {
            g_workThread->detach();
        } else {
            g_workThread->join();
        }
        delete g_workThread;
        g_workThread = nullptr;
    }
    if (g_threadAlphaPng) { MemFree(g_threadAlphaPng); g_threadAlphaPng = nullptr; }
    g_threadAlphaSize = 0;
    g_threadDone      = false;
    g_threadSuccess   = false;
    if (g_sourceRGB.data) { UnloadImage(g_sourceRGB); g_sourceRGB = {0}; }
}

void NNHud_Shutdown(void) {
    CancelWorkThread();
    if (g_sourceRGB.data) { UnloadImage(g_sourceRGB); g_sourceRGB = {0}; }
    if (g_resultRT.id > 0) { UnloadRenderTexture(g_resultRT); g_resultRT = {0}; }
    g_hasResult  = false;
    g_processing = false;
    g_srcLayerIdx = -1;
}

static void ProcessThreadResult(void) {
    if (!g_threadDone) return;
    g_processing = false;

    if (!g_threadSuccess || !g_threadAlphaPng || g_threadAlphaSize == 0) {
        snprintf(g_statusText, sizeof(g_statusText), "Server error or no response");
        CancelWorkThread();
        return;
    }

    // Decode alpha PNG → 8-bit grayscale Image
    Image alphaImg = LoadImageFromMemory(".png", g_threadAlphaPng, (int)g_threadAlphaSize);
    if (!alphaImg.data) {
        snprintf(g_statusText, sizeof(g_statusText), "Failed to decode alpha PNG");
        CancelWorkThread();
        return;
    }

    // Alpha should be grayscale 8-bit; convert if needed
    if (alphaImg.format != PIXELFORMAT_UNCOMPRESSED_GRAYSCALE)
        ImageFormat(&alphaImg, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);

    int w = g_sourceRGB.width, h = g_sourceRGB.height;
    int aw = alphaImg.width, ah = alphaImg.height;

    // If alpha size differs from source, scale (nearest) by copying per-pixel
    if (aw != w || ah != h) {
        Image resized = {0};
        resized.width  = w;
        resized.height = h;
        resized.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
        resized.mipmaps = 1;
        resized.data = malloc((size_t)w * h);
        const uint8_t* asrc = (const uint8_t*)alphaImg.data;
        uint8_t* rdst = (uint8_t*)resized.data;
        for (int y = 0; y < h; y++) {
            int sy = y * ah / h;
            for (int x = 0; x < w; x++) {
                int sx = x * aw / w;
                rdst[y * w + x] = asrc[sy * aw + sx];
            }
        }
        UnloadImage(alphaImg);
        alphaImg = resized;
    }

    // Apply alpha to kept 16-bit source RGB
    // sourceRGB is R16G16B16A16 (half-float). We overwrite the alpha channel
    // with the 8-bit alpha scaled to half-float.
    const uint8_t* alphaPixels = (const uint8_t*)alphaImg.data;
    uint16_t* src16 = (uint16_t*)g_sourceRGB.data;
    for (int i = 0; i < w * h; i++) {
        // Scale 8-bit → 16-bit half-float: val/255 → FloatToHalf
        float a = alphaPixels[i] / 255.0f;
        src16[i * 4 + 3] = FloatToHalf(a);
    }
    UnloadImage(alphaImg);

    // Upload composited result to resultRT
    if (g_resultRT.id > 0 && (g_resultRT.texture.width != w || g_resultRT.texture.height != h)) {
        UnloadRenderTexture(g_resultRT);
        g_resultRT = {0};
    }
    if (g_resultRT.id == 0)
        g_resultRT = Load16BitRT(w, h);
    if (g_resultRT.id == 0) {
        snprintf(g_statusText, sizeof(g_statusText), "Failed to allocate result RT");
        CancelWorkThread();
        return;
    }

    Texture2D tmpTex = LoadTextureFromImage(g_sourceRGB);
    if (tmpTex.id > 0) {
        BeginTextureMode(g_resultRT);
        ClearBackground(BLANK);
        rlSetBlendMode(RL_BLEND_CUSTOM);
        rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
        DrawTextureRec(tmpTex, Rectangle{0,0,(float)w,(float)-h}, Vector2{0,0}, WHITE);
        rlSetBlendMode(RL_BLEND_ALPHA);
        EndTextureMode();
        UnloadTexture(tmpTex);
    }

    snprintf(g_statusText, sizeof(g_statusText), "Result ready");
    g_hasResult = true;
    CleanupThreadOnly(); // frees thread + alpha PNG, keeps g_sourceRGB
}

// ── Module ───────────────────────────────────────────────────────────

bool NNHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (!ImGui::IsAnyItemActive() && input.KeyPressed(KEY_THREE)) {
        if (g_activeHud == HUD_NN) {
            HudSetActive(state, HUD_NONE);
        } else {
            HudSetActive(state, HUD_NN);
        }
        return true;
    }
    if (g_activeHud != HUD_NN) return false;
    if (ImGui::IsAnyItemHovered()) {
        input.mouseCaptured = true;
        return true;
    }
    return false;
}

void NNHudModule::DrawGL(const DrawRect& rect) {
    if (g_activeHud != HUD_NN) return;
    PollProgress();
    if (g_threadDone) ProcessThreadResult();

    int count = LayerStack_Count();
    if (count < 1) return;
    int activeIdx = state->activeLayer;
    if (activeIdx < 0 || activeIdx >= count) return;
    int belowIdx = activeIdx - 1;
    RenderTexture2D curRT = LayerStack_GetRT(activeIdx);
    RenderTexture2D belowRT = (belowIdx >= 0) ? LayerStack_GetRT(belowIdx) : RenderTexture2D{0};

    // Layout: 3 squares side-by-side at top-center of viewport
    float pad = 10.0f;
    float previewSz = 240.0f;
    float spacing = 8.0f;
    float totalW = 3.0f * previewSz + 2.0f * spacing;
    float startX = rect.x + (rect.w - totalW) * 0.5f;
    float startY = rect.y + pad;
    float labelH = 22.0f;

    struct Preview { Texture2D tex; int tw, th; const char* label; };
    Preview pre[3] = {
        {belowRT.texture, belowRT.texture.width, belowRT.texture.height, "RGB Source"},
        {curRT.texture,   curRT.texture.width,   curRT.texture.height,   "Trimask"},
        {{0}, 0, 0, "Result"},
    };
    if (g_hasResult && g_resultRT.id > 0) {
        pre[2].tex = g_resultRT.texture;
        pre[2].tw  = g_resultRT.texture.width;
        pre[2].th  = g_resultRT.texture.height;
    }

    for (int i = 0; i < 3; i++) {
        float px = startX + i * (previewSz + spacing);
        DrawRectangleLinesEx(Rectangle{px, startY, previewSz, previewSz}, 1, WHITE);
        if (pre[i].tex.id > 0 && pre[i].tw > 0 && pre[i].th > 0) {
            float scale = fminf(previewSz / pre[i].tw, previewSz / pre[i].th);
            float dw = pre[i].tw * scale;
            float dh = pre[i].th * scale;
            float dx = px + (previewSz - dw) * 0.5f;
            float dy = startY + (previewSz - dh) * 0.5f;
            DrawTexturePro(pre[i].tex,
                Rectangle{0,0,(float)pre[i].tw,(float)-pre[i].th},
                Rectangle{dx, dy, dw, dh},
                Vector2{0,0}, 0.0f, WHITE);
        }
        // Label below
        float fontSize = 27.0f;
        float spacing = 2.0f;
        const char* txt = pre[i].label;
        float tw2 = MeasureTextEx(g_dialogFont, txt, fontSize, spacing).x;
        Vector2 labelPos = {px + (previewSz - tw2) * 0.5f, startY + previewSz + 4};
        DrawTextEx(g_dialogFont, txt, {labelPos.x + 2, labelPos.y + 2}, fontSize, spacing, {0,0,0,160});
        DrawTextEx(g_dialogFont, txt, labelPos, fontSize, spacing, WHITE);
    }
}

void NNHudModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_NN) return;
    PollProgress();
    if (g_threadDone) ProcessThreadResult();

    int count = LayerStack_Count();
    bool canGetMask = (state->activeLayer >= 1 && !g_processing && count >= 2);
    bool canAccept  = g_hasResult && g_resultRT.id > 0 && g_srcLayerIdx >= 0;

    // Position toolbar below the preview squares
    float pad = 10.0f;
    float previewSz = 240.0f;
    float spacing = 8.0f;
    float totalW = 3.0f * previewSz + 2.0f * spacing;
    float startX = rect.x + (rect.w - totalW) * 0.5f;
    float labelH = 22.0f;
    float btnY = rect.y + pad + previewSz + labelH + 6.0f;

    ImGui::SetNextWindowPos(ImVec2(startX, btnY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(totalW, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("##nnops", NULL,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    ImGui::BeginDisabled(!canGetMask);
    if (ImGui::Button("Get Mask", ImVec2(totalW * 0.45f, 28))) {
        CancelWorkThread(); // cancel any previous in-flight work + free old sourceRGB
        g_processing = true;
        g_hasResult  = false;
        g_statusText[0] = '\0';

        int activeIdx = state->activeLayer;
        int belowIdx  = activeIdx - 1;
        g_srcLayerIdx = belowIdx;

        // Capture source RGB on main thread (GPU read)
        g_sourceRGB = LoadImageFromTexture(LayerStack_GetRT(belowIdx).texture);

        // Export RGB source as 8-bit RGBA PNG
        Image rgb8 = ImageCopy(g_sourceRGB);
        ImageFormat(&rgb8, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        int rgbSize = 0;
        uint8_t* rgbPng = ExportImageToMemory(rgb8, ".png", &rgbSize);
        UnloadImage(rgb8);

        // Export current layer as grayscale trimap PNG (red channel)
        RenderTexture2D curRT = LayerStack_GetRT(activeIdx);
        Image curImg = LoadImageFromTexture(curRT.texture);
        ImageFormat(&curImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        int cw = curImg.width, ch = curImg.height;
        Image gray = {0};
        gray.width = cw; gray.height = ch;
        gray.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
        gray.mipmaps = 1;
        gray.data = malloc((size_t)cw * ch);
        const uint8_t* csrc = (const uint8_t*)curImg.data;
        uint8_t* gdst = (uint8_t*)gray.data;
        for (int i = 0; i < cw * ch; i++)
            gdst[i] = csrc[i * 4]; // red channel
        int triSize = 0;
        uint8_t* triPng = ExportImageToMemory(gray, ".png", &triSize);
        free(gray.data);
        UnloadImage(curImg);

        // Launch worker thread (socket I/O only, no GPU calls)
        g_threadDone = false;
        g_workThread = new std::thread(MatteWorker,
            MatteWork{rgbPng, (size_t)rgbSize, triPng, (size_t)triSize});
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!canAccept);
    if (ImGui::Button("Accept Result", ImVec2(totalW * 0.45f, 28))) {
        // Duplicate source layer (preserves name, blend mode, etc.)
        // then upload g_sourceRGB (which has NN alpha baked in) into it.
        // g_sourceRGB comes from LoadImageFromTexture (bottom-up), but
        // UploadToGPU expects top-down — flip to compensate.
        if (g_sourceRGB.data) ImageFlipVertical(&g_sourceRGB);
        int newIdx = ViewportManager_AcceptMatte(g_srcLayerIdx, g_sourceRGB);
        g_sourceRGB = {0};
        g_srcLayerIdx = -1;
        if (newIdx >= 0) {
            state->activeLayer = newIdx;
            layersDirty = true;
        }
        g_hasResult = false;
    }
    ImGui::EndDisabled();

    // Status/error text
    if (g_processing) {
        ImGui::TextColored(ImVec4(1,1,0,1), "Processing...");
    } else if (g_statusText[0]) {
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1), "%s", g_statusText);
    }

    ImGui::End(); // ##nnops
}

void NNHudModule::OnExit(void) {
    CancelWorkThread();
    if (g_sourceRGB.data) { UnloadImage(g_sourceRGB); g_sourceRGB = {0}; }
    g_processing = false;
    g_srcLayerIdx = -1;
}
