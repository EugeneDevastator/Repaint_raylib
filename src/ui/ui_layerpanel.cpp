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
            if (state->activeLayer >= state->canvas.layerCount)
                state->activeLayer = state->canvas.layerCount - 1;
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
            if (state->activeLayer >= state->canvas.layerCount)
                state->activeLayer = state->canvas.layerCount - 1;
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
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int layerCount = state->canvas.layerCount;

    ImGui::SetNextWindowPos(ImVec2((float)(sw - RIGHT_PANEL_WIDTH), 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)RIGHT_PANEL_WIDTH, (float)sh), ImGuiCond_Always);

    ImGui::Begin("Layers", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float aw = ImGui::GetContentRegionAvail().x;
    float bw = (aw - 12.0f) / 5.0f;
    if (ImGui::Button("+Add", ImVec2(bw, 36))) {
        d_LAction lact = {};
        lact.ActID = laAdd;
        lact.layer = (int16_t)(state->activeLayer + 1);
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Dup", ImVec2(bw, 36)) && layerCount < 64) {
        d_LAction lact = {};
        lact.ActID = laDup;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Drop", ImVec2(bw, 36)) && state->activeLayer > 0) {
        d_LAction lact = {};
        lact.ActID = laDrop;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }
    ImGui::SameLine();
    if (ImGui::Button("Seamless", ImVec2(bw, 36)) && state->activeLayer > 0) {
        LayerStack_MergeDownSeamless(state->activeLayer);
        if (state->activeLayer >= state->canvas.layerCount)
            state->activeLayer = state->canvas.layerCount - 1;
        layersDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Del", ImVec2(bw, 36)) && layerCount > 1) {
        d_LAction lact = {};
        lact.ActID = laDel;
        lact.layer = (int16_t)state->activeLayer;
        CommitLayerOp(state, &lact);
    }

    ImGui::Spacing();

    {
        static const char* blendNames[] = {
            "N-Gamma","N-Linear","Screen","Color Dodge",
            "Lighten","Darken","Burn","Multiply","Overlay","Color"
        };
        int blend = state->canvas.layerProps[state->activeLayer].blendmode;
        if (blend < 0 || blend >= 10) blend = 0;
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##blend", &blend, blendNames, 10, 10)) {
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
        float thresh = state->canvas.layerProps[state->activeLayer].threshold;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##thresh", &thresh, 0.0f, 1.0f, "Threshold %.2f")) {
            Canvas_SetLayerThreshold(&state->canvas, state->activeLayer, thresh);
            layersDirty = true;
        }
    }

    {
        float feather = state->canvas.layerProps[state->activeLayer].feather;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##feather", &feather, 0.0f, 1.0f, "Feather %.2f")) {
            Canvas_SetLayerFeather(&state->canvas, state->activeLayer, feather);
            layersDirty = true;
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
                        ImVec2(ts, ts), ImVec2(0, 1), ImVec2(1, 0));
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

    // ── Brush textures ────────────────────────────────────────────────
    {
        ImGui::Separator();
        float aw2 = ImGui::GetContentRegionAvail().x;
        ImGui::Text("Textures");
        ImGui::PushID("tex");
        ImGui::SameLine(aw2 - 72);
        if (ImGui::SmallButton("+Add") && state->brushTexCount < MAX_BRUSH_TEX) {
            char name[64];
            snprintf(name, sizeof(name), "Texture %d", state->brushTexCount + 1);
            int idx = BrushTex_Add(state, name, 512, 512);
            if (idx >= 0) { state->editTexMode = 1; state->activeBrushTex = idx; }
        }
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton(" -Del") && state->activeBrushTex >= BUILTIN_TEX_COUNT) {
            BrushTex_Delete(state, state->activeBrushTex);
            if (state->brushTexCount <= BUILTIN_TEX_COUNT) { state->editTexMode = 0; state->activeBrushTex = -1; }
        }

        int texCols = (int)(aw2 / 70.0f);
        if (texCols < 2) texCols = 2;
        int texSz = (int)((aw2 - (texCols - 1) * 4) / texCols);
        if (texSz > 64) texSz = 64;
        if (texSz < 32) texSz = 32;

        bool isNone = (state->activeBrushTex < 0 && state->editTexMode == 0);
        if (isNone) { ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1)); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.4f,0.4f,1)); }
        if (ImGui::Button("Canvas", ImVec2(texSz, texSz))) {
            state->editTexMode = 0;
            state->activeBrushTex = -1;
        }
        if (isNone) { ImGui::PopStyleColor(2); }

        for (int ti = BUILTIN_TEX_COUNT; ti < state->brushTexCount; ti++) {
            if ((ti - BUILTIN_TEX_COUNT + 1) % texCols != 0) ImGui::SameLine(0, 4);
            ImGui::PushID(600 + ti);
            Texture2D thumb = BrushTex_GetThumb(state, ti);
            bool isSel = (state->activeBrushTex == ti && state->editTexMode == 1);
            if (isSel) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f,1.0f,0.2f,1));
            if (thumb.id > 0) {
                if (ImGui::ImageButton("##bt", (ImTextureID)(intptr_t)thumb.id, ImVec2(texSz, texSz))) {
                    state->editTexMode = 1;
                    state->activeBrushTex = ti;
                }
            }
            if (isSel) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::PopID();
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
