#include "repaint.h"

void DrawStamp(RenderTexture2D dstRT, AppState* state) {
    if (dstRT.id == 0) return;
    int cw = dstRT.texture.width;
    int ch = dstRT.texture.height;
    if (cw < 1 || ch < 1) return;
    float cx = state->camera.target.x;
    float cy = state->camera.target.y;

    d_Brush pb;
    memset(&pb, 0, sizeof(pb));
    pb.Realb.rad_out    = BParam_GetValue(&bpSize);
    float h = fminf(BParam_GetValue(&bpHardness), 1.0f);
    pb.Realb.rad_in     = pb.Realb.rad_out * h;
    pb.Realb.crv        = BParam_GetValue(&bpCurvature);
    pb.Realb.texBlendVal  = state->currentBrush.Realb.texBlendVal;
    pb.Realb.texScale     = state->currentBrush.Realb.texScale;
    pb.Realb.texFeather   = state->currentBrush.Realb.texFeather;
    pb.Realb.texThresh    = state->currentBrush.Realb.texThresh;
    pb.Realb.useTexLumAsAlpha = state->currentBrush.Realb.useTexLumAsAlpha;
    pb.Realb.texUseRGB    = state->currentBrush.Realb.texUseRGB;
    pb.Realb.texBlendMode = state->currentBrush.Realb.texBlendMode;
    pb.Realb.texNoisemode = state->currentBrush.Realb.texNoisemode;
    pb.Realb.col        = HSLToRGB(colorHue, colorSat, colorLit);
    pb.Realb.opacity    = BParam_GetValue(&bpOpacity);
    pb.Realb.cop        = 0.0f;
    pb.Realb.bmidx      = state->currentBrush.Realb.bmidx;
    pb.Realb.x2y        = state->currentBrush.Realb.x2y;
    pb.Realb.sol        = 1.0f;
    pb.Realb.sol2op     = 0.0f;
    pb.Realb.resangle   = state->currentBrush.Realb.resangle;
    pb.Realb.seed       = 0;

    Texture2D savedTex = g_activeBrushTex;
    if (state->activeBrushTex >= 0 && state->activeBrushTex < state->brushTexCount)
        g_activeBrushTex = state->brushTex[state->activeBrushTex].rt.texture;
    else
        g_activeBrushTex = Texture2D{0};

    BrushBlend_ApplyStamp(dstRT, &pb, cx, cy, cx, cy);
    g_activeBrushTex = savedTex;
}
