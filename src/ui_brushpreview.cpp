#include "repaint.h"
#include "imgui.h"

static RenderTexture2D gizmoStamp = {0};

void BrushPreview_Init(void) {
    if (gizmoStamp.id == 0) {
        gizmoStamp = LoadRenderTexture(256, 256);
        SetTextureFilter(gizmoStamp.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(gizmoStamp.texture, TEXTURE_WRAP_CLAMP);
    }
}

void BrushPreview_Shutdown(void) {
    if (gizmoStamp.id > 0) {
        UnloadRenderTexture(gizmoStamp);
        gizmoStamp.id = 0;
    }
}

void BrushPreview_RenderStamp(AppState* state, float drawRadOut) {
    if (gizmoStamp.id == 0 || drawRadOut <= 1.0f) return;
    float scale = 128.0f / fmaxf(drawRadOut, 1.0f);
    d_Brush pb = state->currentBrush;
    pb.Realb.rad_out = 128.0f;
    pb.Realb.rad_in  = state->currentBrush.Realb.rad_in * state->camera.zoom * scale;
    pb.Realb.bmidx   = 0;
    pb.Realb.cop     = 0.0f;

    BeginTextureMode(gizmoStamp);
    ClearBackground(BLANK);
    EndTextureMode();

    BrushBlend_ApplyStamp(gizmoStamp, &pb, 128, 128, 128, 128);
}

void BrushPreview_DrawStamp(ImDrawList* dl, ImVec2 org, float drawRadOut) {
    if (gizmoStamp.id == 0 || drawRadOut <= 1.0f) return;
    dl->AddImage((ImTextureID)(intptr_t)gizmoStamp.texture.id,
        ImVec2(org.x - drawRadOut, org.y - drawRadOut),
        ImVec2(org.x + drawRadOut, org.y + drawRadOut),
        ImVec2(0, 0), ImVec2(1, 1),
        IM_COL32(255, 255, 255, 255));
}
