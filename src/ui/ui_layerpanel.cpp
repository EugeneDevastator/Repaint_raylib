#include "repaint.h"
#include "layerstack.h"
#include "imgui.h"
#include "network_broker.h"
#include "rlgl.h"
#include <cstdint>

extern bool layersDirty;
extern Texture2D g_blendModeIcon;
extern bool g_blendIconLoaded;

// Texture list state — which user texture is selected in the layer panel
static int g_layerTexSelected = -1;

// ── Layer preview texture cache ──────────────────────────────────────
#define MAX_PREVIEWS 64
static RenderTexture2D g_previewTex[MAX_PREVIEWS] = {0};
static int g_previewCount = 0;
static int g_previewW = 0, g_previewH = 0;

// Called from App_Draw after DrawGL — renders each layer with its
// transform into a small preview RT matching canvas proportion.
void LayerPanel_UpdatePreviews(AppState* state) {
    int count = LayerStack_Count();
    int cw = DocOutW(&state->doc), ch = DocOutH(&state->doc);
    if (cw < 1 || ch < 1 || count < 1) return;

    int pw = (int)(36.0f * cw / ch);
    if (pw < 36) pw = 36;
    if (pw > 150) pw = 150;
    int ph = 36;

    bool sizeChanged = (pw != g_previewW || ph != g_previewH);
    bool countChanged = (count != g_previewCount);

    if (sizeChanged || countChanged) {
        for (int i = 0; i < g_previewCount; i++)
            if (g_previewTex[i].id) UnloadRenderTexture(g_previewTex[i]);
        memset(g_previewTex, 0, sizeof(g_previewTex));
        g_previewCount = count;
        g_previewW = pw;
        g_previewH = ph;
        for (int i = 0; i < count; i++)
            g_previewTex[i] = Load16BitRT(pw, ph);
    }

    float sx = (float)pw / (float)cw;
    float sy = (float)ph / (float)ch;
    for (int i = 0; i < count; i++) {
        if (g_previewTex[i].id == 0) continue;
        RenderTexture2D dst = g_previewTex[i];
        Texture2D src = LayerStack_GetRT(i).texture;
        sLayerProps* p = LayerStack_GetProps(i);
        if (src.id == 0) continue;
        BeginTextureMode(dst); ClearBackground(BLANK);
        rlSetBlendMode(RL_BLEND_CUSTOM); rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
        rlPushMatrix();
        float* M = p->mat;
        float mg[16]={sx*M[0],sy*M[3],0,0, sx*M[1],sy*M[4],0,0, 0,0,1,0, sx*M[2],sy*M[5],0,1};
        rlMultMatrixf(mg);
        DrawTextureRec(src,Rectangle{0,0,(float)p->layerW,(float)-p->layerH},Vector2{0,0},WHITE);
        rlPopMatrix();
        EndTextureMode();
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
}

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
    float bw = (aw - 12.0f) / 5.0f;
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
    if (ImGui::Button("Inst", ImVec2(bw, 36)) && LayerStack_Count() < 64 && state->activeLayer >= 0) {
        LayerStack_DuplicateAsInstance(state->activeLayer);
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
        int blend = LayerStack_GetProps(state->activeLayer)->blendmode;
        if (blend < 0 || blend >= g_blendModeCount) blend = 0;
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##blend", &blend, g_blendModeNames, g_blendModeCount, g_blendModeCount)) {
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
    {
        const char* frameLabel = (state->framingMode == FRAME_CROP) ? "Framing: Crop" : "Framing: Canvas";
        if (ImGui::Button(frameLabel, ImVec2(-1, 0))) {
            bool enteringCrop = (state->framingMode == FRAME_DEFAULT);
            if (enteringCrop) {
                HudSetActive(state, HUD_CANVAS_XFORM);
                state->framingMode = FRAME_CROP;
                Rectangle sb; LayerStack_GetSceneBounds(&sb);
                if (sb.width > 0 && sb.height > 0) {
                    float cx = sb.x + sb.width/2, cy = sb.y + sb.height/2;
                    state->camera.target = Vector2{cx, cy};
                    float pad = 1.1f;
                    float zoomX = viewport.bounds.width / (sb.width * pad);
                    float zoomY = viewport.bounds.height / (sb.height * pad);
                    state->camera.zoom = fminf(zoomX, zoomY);
                } else {
                    state->camera.target = Vector2{0,0};
                    state->camera.zoom = 1.0f;
                }
            } else {
                HudSetActive(state, HUD_NONE);
                state->framingMode = FRAME_DEFAULT;
                state->camera.target = Vector2{state->doc.window.mat[2], state->doc.window.mat[5]};
                state->camera.zoom = 1.0f;
            }
            layersDirty = true;
        }
    }

    ImGui::Separator();

    {
        float avail = ImGui::GetContentRegionAvail().y;
        float listH = avail * 0.35f;
        if (listH < 10.0f) listH = 10.0f;
        if (ImGui::BeginChild("LayerList", ImVec2(0, listH), false)) {
            float prevRMaxY = ImGui::GetCursorScreenPos().y;
            for (int i = 0; i < LayerStack_Count(); i++) {
                int idx = LayerStack_Count() - 1 - i;
                bool isActive = (idx == state->activeLayer);

                ImGui::PushID(idx);
                float itemLeftX = ImGui::GetCursorScreenPos().x;

                bool vis = LayerStack_GetProps(idx)->visible;
                if (ImGui::Checkbox("##v", &vis)) {
                    LayerStack_GetProps(idx)->visible = vis;
                    layersDirty = true;
                }
                ImGui::SameLine();

                if (idx < LayerStack_Count() && g_previewTex[idx].id > 0) {
                    float pw = (float)g_previewW, ph = (float)g_previewH;
                    ImGui::Image((ImTextureID)(intptr_t)g_previewTex[idx].texture.id,
                        ImVec2(pw, ph), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::SameLine();
                }

                char lname[256];
                const char* ln = LayerStack_GetProps(idx)->layerName;
                if (ln[0])
                    snprintf(lname, sizeof(lname), "%s", ln);
                else
                    snprintf(lname, sizeof(lname), "Layer %d", idx + 1);

                ImVec2 selSize = ImVec2(0, 36);
                bool dragging = ImGui::GetDragDropPayload()
                    && ImGui::GetDragDropPayload()->IsDataType("LAYER_IDX");
                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.50f, 0.95f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.55f, 0.95f, 0.9f));
                } else if (dragging) {
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
                }
                if (ImGui::Selectable(lname, isActive, 0, selSize)) {
                    state->activeLayer = idx;
                    if (state->editTexMode) state->editTexMode = 0;
                }
                if (isActive) ImGui::PopStyleColor(2);
                else if (dragging) ImGui::PopStyleColor(1);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("LAYER_IDX", &i, sizeof(int));
                    ImGui::Text("Move %s", lname);
                    ImGui::EndDragDropSource();
                }

                if (dragging)
                    ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0,0,0,0));
                if (ImGui::BeginDragDropTarget()) {
                    const ImGuiPayload* dragPld = ImGui::GetDragDropPayload();
                    if (dragPld && dragPld->IsDataType("LAYER_IDX")) {
                        ImVec2 rMin = ImGui::GetItemRectMin(), rMax = ImGui::GetItemRectMax();
                        float sp = ImGui::GetStyle().ItemSpacing.y;
                        float lineY = (ImGui::GetMousePos().y < (rMin.y + rMax.y) * 0.5f)
                            ? (prevRMaxY + rMin.y) * 0.5f
                            : rMax.y + sp * 0.5f;
                        lineY = (float)(int)(lineY + 0.5f);
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(itemLeftX, lineY), ImVec2(rMax.x, lineY),
                            IM_COL32(50, 130, 255, 220), 5.0f);
                    }
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
                if (dragging)
                    ImGui::PopStyleColor();

                ImGui::PopID();
                prevRMaxY = ImGui::GetItemRectMax().y;
            }
        }
        ImGui::EndChild();
    }

    // ── User texture section ──
    ImGui::Separator();

    ImGui::PushID("tex");
    {
        float aw = ImGui::GetContentRegionAvail().x;
        float btnW = (aw - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (btnW < 30.0f) btnW = 30.0f;

        if (ImGui::Button("+Tex", ImVec2(btnW, 24))) {
            char name[64];
            snprintf(name, sizeof(name), "Texture %d", TM_Count(TM_BUCKET_USER) + 1);
            TexSlotID id = BrushTex_Add(state, name, 512, 512);
            if (TM_IsValid(id)) {
                g_layerTexSelected = id.slot;
                state->editTexMode = 1;
                state->editTexSlot = id;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Dup", ImVec2(btnW, 24))) {
            if (g_layerTexSelected >= 0) {
                TexSlotID srcId = {TM_BUCKET_USER, (uint8_t)g_layerTexSelected};
                TexSlot* srcTs = TM_Get(srcId);
                if (srcTs && TM_IsValid(srcId)) {
                    TexSlotID di = BrushTex_Add(state, srcTs->name, srcTs->w, srcTs->h);
                    if (TM_IsValid(di)) {
                        TexSlot* dstTs = TM_Get(di);
                        if (dstTs) {
                            BeginTextureMode(dstTs->rt);
                            ClearBackground(BLANK);
                            rlSetBlendMode(RL_BLEND_CUSTOM);
                            rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                            DrawTextureRec(srcTs->rt.texture,
                                Rectangle{0, 0, (float)srcTs->w, (float)-srcTs->h},
                                Vector2{0, 0}, WHITE);
                            rlSetBlendMode(RL_BLEND_ALPHA);
                            EndTextureMode();
                            g_layerTexSelected = di.slot;
                            state->editTexMode = 1;
                            state->editTexSlot = di;
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(btnW, 24))) {
            if (g_layerTexSelected >= 0) {
                TexSlotID id = {TM_BUCKET_USER, (uint8_t)g_layerTexSelected};
                TexSlot* ts = TM_Get(id);
                if (ts && !ts->builtIn) {
                    BrushTex_Delete(state, id);
                    g_layerTexSelected = -1;
                }
            }
        }

        float texAvail = ImGui::GetContentRegionAvail().y;
        float texListH = texAvail * 0.65f;
        if (texListH < 10.0f) texListH = 10.0f;
        if (ImGui::BeginChild("UserTexList", ImVec2(0, texListH), false)) {
            for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++) {
                TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
                TexSlot* ts = TM_Get(id);
                if (!ts || ts->builtIn) continue;

                ImGui::PushID(s);
                bool isEditing = (state->editTexMode && state->editTexSlot == id);
                bool isSel = (g_layerTexSelected == s);

                Texture2D thumb = BrushTex_GetThumb(state, id);
                if (thumb.id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)thumb.id, ImVec2(36, 36), ImVec2(0, 1), ImVec2(1, 0));
                    ImGui::SameLine();
                }

                if (isEditing) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.50f, 0.95f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.55f, 0.95f, 0.9f));
                }
                if (ImGui::Selectable(ts->name, isSel, 0, ImVec2(0, 36))) {
                    g_layerTexSelected = s;
                    if (state->editTexMode && state->editTexSlot == id) {
                        state->editTexMode = 0;
                    } else {
                        state->editTexMode = 1;
                        state->editTexSlot = id;
                    }
                }
                if (isEditing) ImGui::PopStyleColor(2);

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::PopID();

    // Network lobby panel (bottom of layers)
    {
        ImGui::Separator();
        float netH = ImGui::GetContentRegionAvail().y;
        if (netH < 10.0f) netH = 10.0f;
        if (ImGui::BeginChild("NetworkLobby", ImVec2(0, netH), false)) {
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
