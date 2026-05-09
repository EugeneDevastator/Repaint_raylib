#include "repaint.h"
#include "rlImGui.h"
#include "imgui.h"

extern bool layersDirty;

void LayerPanel_Init(void) {
    rlImGuiSetup(true);
}

void LayerPanel_Shutdown(void) {
    rlImGuiShutdown();
}

void LayerPanel_Draw(AppState* state) {
    int layerCount = state->canvas.layerCount;

    rlImGuiBegin();

    ImGui::SetNextWindowPos(ImVec2((float)RIGHT_PANEL_X, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)RIGHT_PANEL_WIDTH, (float)SCREEN_HEIGHT), ImGuiCond_Always);
    ImGui::Begin("Layers", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("Layers");
    ImGui::Separator();

    if (ImGui::Button("+ Add")) {
        Canvas_InsertLayer(&state->canvas, state->activeLayer + 1);
        SyncAllRTs(state);
        layersDirty = true;
        layerCount = state->canvas.layerCount;
    }
    ImGui::SameLine();
    if (ImGui::Button("- Del") && layerCount > 1) {
        Canvas_DeleteLayer(&state->canvas, state->activeLayer);
        if (state->activeLayer >= state->canvas.layerCount)
            state->activeLayer = state->canvas.layerCount - 1;
        layersDirty = true;
        layerCount = state->canvas.layerCount;
    }

    ImGui::Spacing();

    // Opacity
    float op = state->canvas.layerProps[state->activeLayer].op;
    ImGui::Text("Opacity");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##op", &op, 0.0f, 1.0f)) {
        Canvas_SetLayerOpacity(&state->canvas, state->activeLayer, op);
        layersDirty = true;
    }

    // Blend mode
    static const char* blendNames[] = {
        "Normal","Add","Dodge","Screen","Lighten","Burn",
        "Multiply","Darken","Overlay","Highlight","Shadowlight","Xor","Diff","Exclusion"
    };
    int blend = state->canvas.layerProps[state->activeLayer].blendmode;
    ImGui::Text("Blend");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##blend", &blend, blendNames, 14)) {
        Canvas_SetLayerBlendMode(&state->canvas, state->activeLayer, blend);
        layersDirty = true;
    }

    ImGui::Separator();

    // Layer list
    for (int i = 0; i < layerCount; i++) {
        int idx = layerCount - 1 - i;
        bool isActive = (idx == state->activeLayer);

        ImGui::PushID(idx);

        bool vis = state->canvas.layerProps[idx].visible;
        if (ImGui::Checkbox("##v", &vis)) {
            state->canvas.layerProps[idx].visible = vis;
            layersDirty = true;
        }
        ImGui::SameLine();

        char lname[32];
        snprintf(lname, sizeof(lname), "Layer %d", idx + 1);

        if (isActive) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f,0.65f,0.85f,1.0f));
        if (ImGui::Selectable(lname, isActive, 0, ImVec2(0, 40)))
            state->activeLayer = idx;
        if (isActive) ImGui::PopStyleColor();

        // Drag-drop reorder
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("LAYER_IDX", &i, sizeof(int));
            ImGui::Text("Move %s", lname);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LAYER_IDX");
            if (payload) {
                int fromVis = *(int*)payload->Data;
                int fromIdx = layerCount - 1 - fromVis;
                if (fromIdx != idx) {
                    Canvas_MoveLayer(&state->canvas, fromIdx, idx);
                    state->activeLayer = idx;
                    SyncAllRTs(state);
                    layersDirty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }

    ImGui::End();
    rlImGuiEnd();
}

void LayerPanel_HandleInput(AppState* state, Vector2 mousePos) {
    (void)state; (void)mousePos;
}
