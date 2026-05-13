#include "repaint.h"
#include "imgui.h"
#include "network_broker.h"
#include <cstdint>

extern bool layersDirty;
extern Texture2D g_blendModeIcon;
extern bool g_blendIconLoaded;

static void CommitLayerOp(AppState* state, d_LAction* lact) {
    if (networkBroker.IsConnected()) {
        networkBroker.SendLAction(lact);
        // server-authoritative: don't apply locally, wait for echo
    } else {
        // local mode: apply directly
        switch (lact->ActID) {
        case laAdd: {
            int insertAfter = lact->layer;
            Canvas_InsertLayer(&state->canvas, insertAfter);
            SyncAllRTs(state);
            if (state->activeLayer >= insertAfter)
                state->activeLayer++;
            break;
        }
        case laDel: {
            int idx = lact->layer;
            Canvas_DeleteLayer(&state->canvas, idx);
            if (state->activeLayer >= state->canvas.layerCount)
                state->activeLayer = state->canvas.layerCount - 1;
            SyncAllRTs(state);
            break;
        }
        case laDup: {
            int idx = lact->layer;
            Canvas_DuplicateLayer(&state->canvas, idx);
            state->activeLayer = idx + 1;
            EnsureRTs(state);
            SyncRTFromImage(state, state->activeLayer);
            break;
        }
        case laDrop: {
            int idx = lact->layer;
            MergeDownLayer(state, idx);
            if (state->activeLayer >= state->canvas.layerCount)
                state->activeLayer = state->canvas.layerCount - 1;
            break;
        }
        case laMove: {
            Canvas_MoveLayer(&state->canvas, lact->layer, lact->layerto);
            state->activeLayer = lact->layerto;
            SyncAllRTs(state);
            break;
        }
        default: break;
        }
        layersDirty = true;
    }
}

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
        d_LAction lact = {};
        lact.ActID = laAdd;
        lact.layer = (int16_t)(state->activeLayer + 1);
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Dup", ImVec2(bw, 28)) && layerCount < 64) {
        d_LAction lact = {};
        lact.ActID = laDup;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Drop", ImVec2(bw, 28)) && state->activeLayer > 0) {
        d_LAction lact = {};
        lact.ActID = laDrop;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Del", ImVec2(bw, 28)) && layerCount > 1) {
        d_LAction lact = {};
        lact.ActID = laDel;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
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
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##blend", &blend, blendNames, 14, 14)) {
            Canvas_SetLayerBlendMode(&state->canvas, state->activeLayer, blend);
            layersDirty = true;
            if (networkBroker.IsConnected()) {
                d_LAction lact = {};
                lact.ActID = laBm;
                lact.layer = (int16_t)state->activeLayer;
                lact.bm = (uint8_t)blend;
                networkBroker.SendLAction(&lact);
            }
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
        if (ImGui::IsItemDeactivatedAfterEdit() && networkBroker.IsConnected()) {
            d_LAction lact = {};
            lact.ActID = laOp;
            lact.layer = (int16_t)state->activeLayer;
            lact.op = op;
            networkBroker.SendLAction(&lact);
        }
    }

    ImGui::Spacing();

    {
        char idxBuf[64];
        snprintf(idxBuf, sizeof(idxBuf), "Layer %d / %d", state->activeLayer + 1, layerCount);
        ImGui::Text("%s", idxBuf);
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

                if (idx < state->texCount && state->layerRTs[idx].id > 0) {
                    float ts = 36.0f;
                    ImGui::Image((ImTextureID)(intptr_t)state->layerRTs[idx].texture.id,
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
                            d_LAction lact = {};
                            lact.ActID = laMove;
                            lact.layer = (int16_t)fromIdx;
                            lact.layerto = (int16_t)idx;
                            CommitLayerOp(state, &lact);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    // Network lobby panel (bottom of layers)
    {
        ImGui::Separator();
        float netH = ImGui::GetContentRegionAvail().y;
        if (netH < 10.0f) netH = 10.0f;
        if (ImGui::BeginChild("NetworkLobby", ImVec2(0, netH), false,
                ImGuiWindowFlags_NoScrollbar)) {
            ImGui::Text("Server");
            ImGui::Separator();
            float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Server", ImVec2(btnW, 0)))
                networkBroker.showUI = !networkBroker.showUI;
            ImGui::SameLine();
            if (networkBroker.IsConnected()) {
                if (ImGui::Button("Disconnect", ImVec2(btnW, 0)))
                    networkBroker.Disconnect();
            } else {
                if (ImGui::Button("Room", ImVec2(btnW, 0))) {}
            }
            if (networkBroker.IsConnected())
                ImGui::Text("IP: %s:%d", networkBroker.serverAddr, networkBroker.serverPort);
            else
                ImGui::Text("Not connected");
            ImGui::Separator();
            ImGui::Text("Users (%d):", networkBroker.userCount);
            for (int i = 0; i < networkBroker.userCount; i++) {
                const char* name = networkBroker.userNames[i];
                bool isMe = strcmp(name, networkBroker.ownName) == 0;
                if (isMe) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.3f, 1.0f));
                    ImGui::Text("> %s", name);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::Text("  %s", name);
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
