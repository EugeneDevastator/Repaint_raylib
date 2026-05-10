#include "repaint.h"
#include "imgui.h"
#include <cstdint>

extern bool layersDirty;

void LayerPanel_Draw(AppState* state) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int layerCount = state->canvas.layerCount;

    ImGui::SetNextWindowPos(ImVec2((float)(sw - RIGHT_PANEL_WIDTH), 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)RIGHT_PANEL_WIDTH, (float)sh), ImGuiCond_Always);

    ImGui::Begin("Layers", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float aw = ImGui::GetContentRegionAvail().x;
    float bw = (aw - 9.0f) / 4.0f;
    if (ImGui::Button("+Add", ImVec2(bw, 28))) {
        Canvas_InsertLayer(&state->canvas, state->activeLayer + 1);
        SyncAllRTs(state);
        layersDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Dup", ImVec2(bw, 28)) && layerCount < 64) {
        Canvas_DuplicateLayer(&state->canvas, state->activeLayer);
        state->activeLayer++;
        EnsureRTs(state);
        SyncRTFromImage(state, state->activeLayer);
        layersDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Drop", ImVec2(bw, 28)) && state->activeLayer > 0) {
        int merged = state->activeLayer;
        Canvas_MergeDown(&state->canvas, merged);
        if (state->activeLayer >= state->canvas.layerCount)
            state->activeLayer = state->canvas.layerCount - 1;
        SyncAllRTs(state);
        layersDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Del", ImVec2(bw, 28)) && layerCount > 1) {
        Canvas_DeleteLayer(&state->canvas, state->activeLayer);
        if (state->activeLayer >= state->canvas.layerCount)
            state->activeLayer = state->canvas.layerCount - 1;
        SyncAllRTs(state);
        layersDirty = true;
    }

    ImGui::Spacing();

    {
        static const char* blendNames[] = {
            "Normal","Add","Dodge","Screen","Lighten","Burn",
            "Multiply","Darken","Overlay","Highlight","Shadowlight",
            "Xor","Diff","Exclusion"
        };
        int blend = state->canvas.layerProps[state->activeLayer].blendmode;
        if (blend < 0 || blend >= 14) blend = 0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##blend", &blend, blendNames, 14, 14)) {
            Canvas_SetLayerBlendMode(&state->canvas, state->activeLayer, blend);
            layersDirty = true;
        }
    }

    ImGui::Spacing();

    {
        float op = state->canvas.layerProps[state->activeLayer].op;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##op", &op, 0.0f, 1.0f, "Opacity %.2f")) {
            Canvas_SetLayerOpacity(&state->canvas, state->activeLayer, op);
            layersDirty = true;
        }
    }

    ImGui::Spacing();

    {
        char idxBuf[64];
        snprintf(idxBuf, sizeof(idxBuf), "Layer %d / %d", state->activeLayer + 1, layerCount);
        ImGui::Text("%s", idxBuf);
    }

    {
        bool presop = state->canvas.layerProps[state->activeLayer].presop != 0;
        if (ImGui::Checkbox("Preserve opacity", &presop)) {
            state->canvas.layerProps[state->activeLayer].presop = presop ? 1 : 0;
        }
    }

    ImGui::Separator();

    {
        float avail = ImGui::GetContentRegionAvail().y;
        float listH = avail * 0.7f;
        if (listH < 10.0f) listH = 10.0f;
        if (ImGui::BeginChild("LayerList", ImVec2(0, listH), false)) {
            for (int i = 0; i < layerCount; i++) {
                int idx = layerCount - 1 - i;
                bool isActive = (idx == state->activeLayer);

                ImGui::PushID(idx);

                bool vis = state->canvas.layerProps[idx].visible;
                if (ImGui::Checkbox("##v", &vis)) {
                    Canvas_SetLayerVisible(&state->canvas, idx, vis);
                    layersDirty = true;
                }
                ImGui::SameLine();

                if (idx < state->texCount && state->layerTextures[idx].id > 0) {
                    float ts = 36.0f;
                    ImGui::Image((ImTextureID)(intptr_t)state->layerTextures[idx].id,
                        ImVec2(ts, ts));
                    ImGui::SameLine();
                }

                char lname[256];
                const char* ln = state->canvas.layerProps[idx].layerName;
                if (ln[0])
                    snprintf(lname, sizeof(lname), "%s", ln);
                else
                    snprintf(lname, sizeof(lname), "Layer %d", idx + 1);

                ImVec2 selSize = ImVec2(0, 36);
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.50f, 0.95f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.55f, 0.95f, 0.9f));
                }
                if (ImGui::Selectable(lname, isActive, 0, selSize))
                    state->activeLayer = idx;
                if (isActive) ImGui::PopStyleColor(2);

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
        }
        ImGui::EndChild();
    }

    // Network lobby panel (dummy UI, bottom 30%)
    {
        ImGui::Separator();
        float netH = ImGui::GetContentRegionAvail().y;
        if (netH < 10.0f) netH = 10.0f;
        if (ImGui::BeginChild("NetworkLobby", ImVec2(0, netH), false,
                ImGuiWindowFlags_NoScrollbar)) {
            ImGui::Text("Server");
            ImGui::Separator();
            float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Server", ImVec2(btnW, 24))) {}
            ImGui::SameLine();
            if (ImGui::Button("Room", ImVec2(btnW, 24))) {}
            ImGui::Text("IP: 0.0.0.0:0");
            ImGui::Separator();
            ImGui::Text("Users:");
            static const char* dummyUsers[] = {"Alice", "Bob", "Charlie"};
            static int selectedUser = -1;
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(dummyUsers[i], selectedUser == i))
                    selectedUser = i;
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
