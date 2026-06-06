#include "repaint.h"
#include "layerstack.h"
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
            state->activeLayer = LayerStack_InsertLayer(lact->layer);
            break;
        }
        case laDel: {
            int idx = lact->layer;
            LayerStack_DeleteLayer(idx);
            if (state->activeLayer >= LayerStack_Count())
                state->activeLayer = LayerStack_Count() - 1;
            break;
        }
        case laDup: {
            int idx = lact->layer;
            LayerStack_DuplicateLayer(idx);
            state->activeLayer = idx + 1;
            break;
        }
        case laDrop: {
            int idx = lact->layer;
            MergeDownLayer(state, idx);
            if (state->activeLayer >= LayerStack_Count())
                state->activeLayer = LayerStack_Count() - 1;
            break;
        }
        case laMove: {
            LayerStack_MoveLayer(lact->layer, lact->layerto);
            state->activeLayer = lact->layerto;
            break;
        }
        default: break;
        }
        layersDirty = true;
    }
}

void LayerPanel_Draw(AppState* state) {
    if (LayerStack_Count() <= 0) return; // no layers yet
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    ImGui::SetNextWindowPos(ImVec2((float)(sw - RIGHT_PANEL_WIDTH), 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)RIGHT_PANEL_WIDTH, (float)sh), ImGuiCond_Always);

    ImGui::Begin("Layers", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float aw = ImGui::GetContentRegionAvail().x;
    float bw = (aw - 9.0f) / 4.0f;
    if (ImGui::Button("+Add", ImVec2(bw, 36))) {
        d_LAction lact = {};
        lact.ActID = laAdd;
        lact.layer = (int16_t)(state->activeLayer + 1);
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Dup", ImVec2(bw, 36)) && LayerStack_Count() < 64) {
        d_LAction lact = {};
        lact.ActID = laDup;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Drop", ImVec2(bw, 36)) && state->activeLayer > 0) {
        sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
        if (lp->seamless) {
            LayerStack_MergeDownSeamless(state->activeLayer);
            if (state->activeLayer >= LayerStack_Count())
                state->activeLayer = LayerStack_Count() - 1;
            layersDirty = true;
        } else {
            d_LAction lact = {};
            lact.ActID = laDrop;
            lact.layer = (int16_t)state->activeLayer;
            CommitLayerOp(state, &lact);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Del", ImVec2(bw, 36)) && LayerStack_Count() > 1) {
        d_LAction lact = {};
        lact.ActID = laDel;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }

    ImGui::Spacing();

    {
        sLayerProps* lp = LayerStack_GetProps(state->activeLayer);
        if (ImGui::Checkbox("Seamless in Canvas", &lp->seamless))
            layersDirty = true;
    }

    ImGui::Spacing();

    {
        static const char* blendNames[] = {
            "N-OKLab","N-Gamma","N-Linear","Screen","Color Dodge",
            "Lighten","Darken","Burn","Multiply","Overlay","Color"
        };
        int blend = LayerStack_GetProps(state->activeLayer)->blendmode;
        if (blend < 0 || blend >= 11) blend = 0;
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##blend", &blend, blendNames, 11, 11)) {
            LayerStack_GetProps(state->activeLayer)->blendmode = blend; layersDirty = true;
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
        float op = LayerStack_GetProps(state->activeLayer)->op;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##op", &op, 0.0f, 1.0f, "Opacity %.2f")) {
            LayerStack_GetProps(state->activeLayer)->op = op;
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
        float thresh = LayerStack_GetProps(state->activeLayer)->threshold;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##thresh", &thresh, 0.0f, 1.0f, "Threshold %.2f")) {
            LayerStack_GetProps(state->activeLayer)->threshold = thresh;
            layersDirty = true;
        }
    }

    {
        float feather = LayerStack_GetProps(state->activeLayer)->feather;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##feather", &feather, 0.0f, 1.0f, "Feather %.2f")) {
            LayerStack_GetProps(state->activeLayer)->feather = feather;
            layersDirty = true;
        }
    }

    ImGui::Spacing();

    {
        char idxBuf[64];
        snprintf(idxBuf, sizeof(idxBuf), "Layer %d / %d", state->activeLayer + 1, LayerStack_Count());
        ImGui::Text("%s", idxBuf);
    }
    ImGui::Checkbox("Use screen res", &g_useViewRes);

    ImGui::Separator();

    {
        float avail = ImGui::GetContentRegionAvail().y;
        float listH = avail * 0.7f;
        if (listH < 10.0f) listH = 10.0f;
        if (ImGui::BeginChild("LayerList", ImVec2(0, listH), false)) {
            for (int i = 0; i < LayerStack_Count(); i++) {
                int idx = LayerStack_Count() - 1 - i;
                bool isActive = (idx == state->activeLayer);

                ImGui::PushID(idx);

                bool vis = LayerStack_GetProps(idx)->visible;
                if (ImGui::Checkbox("##v", &vis)) {
                    LayerStack_GetProps(idx)->visible = vis;
                    layersDirty = true;
                }
                ImGui::SameLine();

                if (idx < LayerStack_Count() && LayerStack_GetRT(idx).id > 0) {
                    float ts = 36.0f;
                    ImGui::Image((ImTextureID)(intptr_t)LayerStack_GetRT(idx).texture.id,
                        ImVec2(ts, ts), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::SameLine();
                }

                char lname[256];
                const char* ln = LayerStack_GetProps(idx)->layerName;
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
                        int fromIdx = LayerStack_Count() - 1 - fromVis;
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

// ── RightPanelModule ──────────────────────────────────────────────────────

bool RightPanelModule::HandleInput(InputState& input, const DrawRect& rect) {
    if (input.mouseCaptured) return false;
    if (!rect.Contains(input.MousePos())) return false;
    input.mouseCaptured = true;
    return true;
}

void RightPanelModule::DrawGUI(const DrawRect& rect) {
    if (rect.w < 1 || rect.h < 1) return;
    LayerPanel_Draw(state);
}
