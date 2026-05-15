#include "repaint.h"
#include "imgui.h"

static RenderTexture2D g_previewRT = {0};

void BrushPreview_Init(void) {
    if (g_previewRT.id == 0) {
        g_previewRT = LoadRenderTexture(256, 256);
        SetTextureFilter(g_previewRT.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(g_previewRT.texture, TEXTURE_WRAP_CLAMP);
    }
}

void BrushPreview_Shutdown(void) {
    if (g_previewRT.id > 0) {
        UnloadRenderTexture(g_previewRT);
        g_previewRT.id = 0;
    }
}

void DrawBrushPreview(AppState* state, ImDrawList* dl, ImVec2 center, float size) {
    if (g_previewRT.id == 0) return;

    float brushSize = fminf(BParam_GetValue(&bpSize), 120.0f);
    float scale = 128.0f / fmaxf(brushSize, 1.0f);

    d_Brush pb;
    memset(&pb, 0, sizeof(pb));
    pb.Realb.rad_out  = brushSize * scale;
    pb.Realb.rad_in   = brushSize * fminf(BParam_GetValue(&bpHardness), 1.0f) * scale;
    pb.Realb.crv      = BParam_GetValue(&bpCurvature);
    pb.Realb.texBlendVal  = state->currentBrush.Realb.texBlendVal;
    pb.Realb.texScale     = state->currentBrush.Realb.texScale;
    pb.Realb.texFeather   = state->currentBrush.Realb.texFeather;
    pb.Realb.texThresh    = state->currentBrush.Realb.texThresh;
    pb.Realb.useTexLumAsAlpha = state->currentBrush.Realb.useTexLumAsAlpha;
    pb.Realb.texUseRGB    = state->currentBrush.Realb.texUseRGB;
    pb.Realb.texBlendMode = state->currentBrush.Realb.texBlendMode;
    pb.Realb.texNoisemode = state->currentBrush.Realb.texNoisemode;
    pb.Realb.col      = HSLToRGB(colorHue, colorSat, colorLit);
    pb.Realb.opacity  = 1.0f;
    pb.Realb.cop      = 0.0f;
    pb.Realb.bmidx    = state->currentBrush.Realb.bmidx;
    pb.Realb.x2y      = 1.0f;
    pb.Realb.sol      = 1.0f;
    pb.Realb.sol2op   = 0.0f;
    pb.Realb.resangle = 0.0f;
    pb.Realb.seed     = 0;

    Texture2D savedTex = g_activeBrushTex;
    if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
        g_activeBrushTex = state->brushTex[state->activeBrushTex].rt.texture;
    else
        g_activeBrushTex = Texture2D{0};

    BeginTextureMode(g_previewRT);
    ClearBackground(BLANK);
    EndTextureMode();
    BrushBlend_ApplyStamp(g_previewRT, &pb, 128, 128, 128, 128);
    g_activeBrushTex = savedTex;

    float half = size * 0.5f;
    dl->AddImage((ImTextureID)(intptr_t)g_previewRT.texture.id,
        ImVec2(center.x - half, center.y - half),
        ImVec2(center.x + half, center.y + half),
        ImVec2(0, 0), ImVec2(1, 1),
        IM_COL32(255, 255, 255, 255));
}
