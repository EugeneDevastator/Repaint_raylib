#include "network_broker.h"
#include "layerstack.h"
#include "serialize.h"
#include "sock_platform.h"
#include "app_config.h"
#include "imgui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ── NetworkBroker ──────────────────────────────────────────────────────── */

NetworkBroker::NetworkBroker() {
    state      = NS_DISCONNECTED;
    sockfd     = SOCK_INVALID;
    appState   = NULL;
    configPath[0] = '\0';
    strcpy(serverAddr, "127.0.0.1");
    serverPort    = NET_PORT;
    ownName[0]    = '\0';
    userCount     = 0;
    statusMsg[0]  = '\0';
    showUI        = false;
    localHead     = 0;
    localTail     = 0;
    recvThread    = NULL;
    threadRunning = false;
    recvPos       = 0;
    recvNeed      = NET_HEADER_SIZE;
    layersDirty   = false;

    unsigned int r = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)this;
    r = r * 1103515245u + 12345u;
    snprintf(username, sizeof(username), "User%04u", (r / 65536u) % 10000u);

    sock_init();
}

NetworkBroker::~NetworkBroker() {
    Disconnect();
    sock_shutdown();
}

/* ── Recv thread ────────────────────────────────────────────────────────── */

void NetworkBroker::RecvThreadFunc(NetworkBroker* self) {
    uint8_t headerBuf[NET_HEADER_SIZE];

    while (self->threadRunning) {
        if (!sock_recv_all(self->sockfd, headerBuf, NET_HEADER_SIZE)) {
            self->threadRunning = false;
            break;
        }

        stNetHead head;
        head.Deserialize(headerBuf);

        if (head.Hsize > (uint32_t)(RECV_BUF_SIZE - NET_HEADER_SIZE)) {
            self->threadRunning = false;
            break;
        }

        uint8_t* payload = (uint8_t*)malloc(head.Hsize + 1);
        if (!payload) break;
        payload[head.Hsize] = '\0';

        if (head.Hsize > 0) {
            if (!sock_recv_all(self->sockfd, payload, head.Hsize)) {
                free(payload);
                self->threadRunning = false;
                break;
            }
        }

        ReceivedPacket pkt;
        pkt.hid  = head.Hid;
        pkt.data = payload;
        pkt.size = head.Hsize;

        {
            std::lock_guard<std::mutex> lock(self->pktMtx);
            self->pktQueue.push(pkt);
        }
    }

    std::lock_guard<std::mutex> lock(self->pktMtx);
    while (!self->pktQueue.empty()) {
        free(self->pktQueue.front().data);
        self->pktQueue.pop();
    }
}

/* ── Connect / Disconnect ───────────────────────────────────────────────── */

bool NetworkBroker::Connect(const char* addr, int port) {
    if (state != NS_DISCONNECTED) return false;

    snprintf(statusMsg, sizeof(statusMsg), "Connecting to %s:%d...", addr, port);

    sockfd = sock_connect(addr, port);
    if (sockfd == SOCK_INVALID) {
        snprintf(statusMsg, sizeof(statusMsg), "Connection failed: %s", sock_last_error());
        return false;
    }

    strncpy(serverAddr, addr, sizeof(serverAddr) - 1);
    serverPort    = port;
    state         = NS_CONNECTING;
    threadRunning = true;
    recvThread    = new std::thread(RecvThreadFunc, this);

    sAuth auth;
    strncpy(auth.uname, username, sizeof(auth.uname) - 1);
    auth.upass[0] = '\0';
    auth.aType    = atLogin;

    uint8_t authBuf[1024];
    size_t  authSz = auth.Serialize(authBuf, sizeof(authBuf));
    SendPacket(sdAuth, authBuf, (uint32_t)authSz);
    state = NS_AUTH_SENT;

    snprintf(statusMsg, sizeof(statusMsg), "Authenticating as %s...", username);
    return true;
}

void NetworkBroker::Disconnect() {
    if (state == NS_DISCONNECTED) return;

    threadRunning = false;
    if (sockfd != SOCK_INVALID) {
        sock_disconnect(sockfd);
        sockfd = SOCK_INVALID;
    }
    if (recvThread && recvThread->joinable()) {
        recvThread->join();
        delete recvThread;
        recvThread = NULL;
    }

    {
        std::lock_guard<std::mutex> lock(pktMtx);
        while (!pktQueue.empty()) {
            free(pktQueue.front().data);
            pktQueue.pop();
        }
    }

    state     = NS_DISCONNECTED;
    ownName[0] = '\0';
    userCount  = 0;
    snprintf(statusMsg, sizeof(statusMsg), "Disconnected");
}

/* ── Send ───────────────────────────────────────────────────────────────── */

void NetworkBroker::SendPacket(uint8_t hid, const uint8_t* data, uint32_t size) {
    if (sockfd == SOCK_INVALID) return;

    stNetHead head;
    head.Hid   = hid;
    head.Hsize = size;

    uint8_t headerBuf[NET_HEADER_SIZE];
    head.Serialize(headerBuf);

    if (!sock_send_all(sockfd, headerBuf, NET_HEADER_SIZE)) {
        Disconnect(); return;
    }
    if (size > 0) {
        if (!sock_send_all(sockfd, data, size)) {
            Disconnect();
        }
    }
}

void NetworkBroker::SendAction(const d_Action* act) {
    uint8_t buf[4096];
    size_t sz = Action_Serialize((d_Action*)act, buf, sizeof(buf));
    if (sz > 0) SendPacket(sdAction, buf, (uint32_t)sz);
}

void NetworkBroker::SendLAction(const d_LAction* lact) {
    uint8_t buf[512];
    size_t sz = LAction_Serialize((d_LAction*)lact, buf, sizeof(buf));
    if (sz > 0) SendPacket(sdLAction, buf, (uint32_t)sz);
}

/* ── ICommandBroker ─────────────────────────────────────────────────────── */

void NetworkBroker::on_input(const BrushDab& e) {
    int next = (localTail + 1) % CMD_CAPACITY;
    if (next == localHead) return;
    if (!appState) return;

    int      layer = appState->activeLayer;

    localQueue[localTail].x     = e.x;
    localQueue[localTail].y     = e.y;
    localQueue[localTail].srcX  = e.srcX;
    localQueue[localTail].srcY  = e.srcY;
    localQueue[localTail].color       = e.brush.col;
    localQueue[localTail].rad_out     = e.brush.rad_out;
    localQueue[localTail].radInRatio  = e.brush.radInRatio;
    localQueue[localTail].opacity     = e.brush.opacity;
    localQueue[localTail].crv         = e.brush.crv;
    localQueue[localTail].x2y         = e.brush.x2y;
    localQueue[localTail].sol         = e.brush.sol;
    localQueue[localTail].sol2op      = e.brush.sol2op;
    localQueue[localTail].resangle    = (float)e.brush.resangle;
    localQueue[localTail].cop         = e.brush.cop;
    localQueue[localTail].texBlendVal  = e.brush.texBlendVal;
    localQueue[localTail].texScale     = e.brush.texScale;
    localQueue[localTail].texFeather   = e.brush.texFeather;
    localQueue[localTail].texThresh    = e.brush.texThresh;
    localQueue[localTail].useTexLumAsAlpha = e.brush.useTexLumAsAlpha;
    localQueue[localTail].texUseRGB    = e.brush.texUseRGB;
    localQueue[localTail].texBlendMode = e.brush.texBlendMode;
    localQueue[localTail].texNoisemode = e.brush.texNoisemode;
    localQueue[localTail].texColorMode = e.brush.texColorMode;
    localQueue[localTail].bmidx       = (int)e.brush.bmidx;
    localQueue[localTail].seed        = e.brush.seed;
    localQueue[localTail].preserveop  = e.brush.preserveop;
    localQueue[localTail].eraseMode   = e.brush.eraseMode;
    localQueue[localTail].perspective = e.brush.perspective;
    localQueue[localTail].userTexOriginX = e.brush.userTexOriginX;
    localQueue[localTail].userTexOriginY = e.brush.userTexOriginY;
    localQueue[localTail].userTexDirection = e.brush.userTexDirection;
    localQueue[localTail].activeLayer = layer;
    localQueue[localTail].targetRT    = LayerStack_GetRT(layer);

    localTail = next;
}

void NetworkBroker::poll(AppState* st) {
    this->appState = st;

    while (localHead != localTail) {
        QueuedNetDab* d = &localQueue[localHead];
        bool applied = false;

        if (d->targetRT.id != 0 && d->activeLayer >= 0 && d->activeLayer < LayerStack_Count()) {
            d_Brush brush = {};
            brush.Realb.radInRatio = d->radInRatio;
            brush.Realb.rad_out  = d->rad_out;
            brush.Realb.opacity  = d->opacity;
            brush.Realb.crv      = d->crv;
            brush.Realb.x2y      = d->x2y;
            brush.Realb.sol      = d->sol;
            brush.Realb.sol2op   = d->sol2op;
            brush.Realb.resangle = d->resangle;
            brush.Realb.cop        = d->cop;
            brush.Realb.texBlendVal  = d->texBlendVal;
            brush.Realb.texScale     = d->texScale;
            brush.Realb.texFeather   = d->texFeather;
            brush.Realb.texThresh    = d->texThresh;
            brush.Realb.useTexLumAsAlpha = d->useTexLumAsAlpha;
            brush.Realb.texUseRGB    = d->texUseRGB;
            brush.Realb.texBlendMode = d->texBlendMode;
            brush.Realb.texNoisemode = d->texNoisemode;
            brush.Realb.texColorMode = d->texColorMode;
            brush.Realb.bmidx      = (uint8_t)d->bmidx;
            brush.Realb.seed       = d->seed;
            brush.Realb.col        = d->color;
            brush.Realb.preserveop = d->preserveop;
            brush.Realb.eraseMode  = d->eraseMode;
            brush.Realb.perspective = d->perspective;
            brush.Realb.userTexOriginX = d->userTexOriginX;
            brush.Realb.userTexOriginY = d->userTexOriginY;
            brush.Realb.userTexDirection = d->userTexDirection;

            RenderTexture2D rt = LayerStack_GetRT(d->activeLayer);
            if (rt.id > 0) {
                BrushBlend_ApplyStamp(rt, &brush, g_activeBrushTex, d->x, d->y, d->srcX, d->srcY);
                applied = true;
            }
        }

        if (applied && this->state == NS_CONNECTED) {
            d_Action act;
            act.ToolID = (uint8_t)st->mode;
            act.Brush  = {};
            act.Brush.Realb.radInRatio = d->radInRatio;
            act.Brush.Realb.rad_out  = d->rad_out;
            act.Brush.Realb.opacity  = d->opacity;
            act.Brush.Realb.crv      = d->crv;
            act.Brush.Realb.x2y      = d->x2y;
            act.Brush.Realb.sol      = d->sol;
            act.Brush.Realb.sol2op   = d->sol2op;
            act.Brush.Realb.resangle = d->resangle;
            act.Brush.Realb.cop      = d->cop;
            act.Brush.Realb.bmidx    = (uint8_t)d->bmidx;
            act.Brush.Realb.seed     = d->seed;
            act.Brush.Realb.col      = d->color;
            act.startseed  = 0;
            act.Noisemode  = 0;
            act.Stroke.pos1 = Vector2{d->x, d->y};
            act.Stroke.pos2 = Vector2{d->srcX, d->srcY};
            act.layer = (uint8_t)d->activeLayer;
            SendAction(&act);
        }
        localHead = (localHead + 1) % CMD_CAPACITY;
    }

    {
        std::unique_lock<std::mutex> lock(pktMtx);
        while (!pktQueue.empty()) {
            ReceivedPacket pkt = pktQueue.front();
            pktQueue.pop();
            lock.unlock();
            ProcessReceived(pkt.hid, pkt.data, pkt.size);
            free(pkt.data);
            lock.lock();
        }
    }
}

/* ── Packet dispatch ────────────────────────────────────────────────────── */

void NetworkBroker::ProcessReceived(uint8_t hid, uint8_t* data, uint32_t size) {
    switch (hid) {
    case sdLogin:
        snprintf(statusMsg, sizeof(statusMsg), "Server requested auth for %s", username);
        break;

    case sdLoginS: {
        char name[256] = "";
        SZstring_unpack(data, size, name, sizeof(name));
        state = NS_CONNECTED;
        strncpy(ownName,      name, sizeof(ownName)      - 1);
        strncpy(username,     name, sizeof(username)     - 1);
        strncpy(userNames[0], name, sizeof(userNames[0]) - 1);
        userCount = 1;
        snprintf(statusMsg, sizeof(statusMsg), "Connected as %s", name);
        showUI = false;
        SaveConfig();
        break;
    }

    case sdAction: {
        if (!appState) break;
        d_Action act;
        if (Action_Deserialize(&act, data, size))
            EnqueueRemoteDab(&act);
        break;
    }

    case sdUserStat: {
        stUserState us;
        us.Deserialize(data, size);
        snprintf(statusMsg, sizeof(statusMsg), "User %s status=%d", us.Uname, us.Ustate);
        break;
    }

    case sdUserAdded: {
        char name[256] = "";
        SZstring_unpack(data, size, name, sizeof(name));
        if (userCount < 64) {
            strncpy(userNames[userCount], name, sizeof(userNames[userCount]) - 1);
            userCount++;
        }
        snprintf(statusMsg, sizeof(statusMsg), "User joined: %s", name);
        break;
    }

    case sdUserDel: {
        char name[256] = "";
        SZstring_unpack(data, size, name, sizeof(name));
        for (int i = 0; i < userCount; i++) {
            if (strcmp(userNames[i], name) == 0) {
                for (int j = i; j < userCount - 1; j++)
                    strcpy(userNames[j], userNames[j + 1]);
                userCount--;
                break;
            }
        }
        snprintf(statusMsg, sizeof(statusMsg), "User left: %s", name);
        break;
    }

    case sdLAction: {
        if (!appState || size < 3) break;
        char   senderName[256] = "";
        size_t nameBytes = SZstring_unpack(data, size, senderName, sizeof(senderName));
        if (nameBytes == 0 || nameBytes >= size) break;
        bool createdByMe = (strcmp(senderName, ownName) == 0);
        d_LAction lact;
        if (!LAction_Deserialize(&lact, data + nameBytes, size - (uint32_t)nameBytes)) break;

        switch (lact.ActID) {
        case laAdd: {
            int insertAfter = lact.layer < 0 ? 0 : lact.layer;
            LayerStack_InsertLayer(insertAfter);
            if (createdByMe)
                appState->activeLayer = insertAfter;
            else if (appState->activeLayer >= insertAfter)
                appState->activeLayer++;
            layersDirty = true;
            break;
        }
        case laDel: {
            int idx = lact.layer;
            if (idx < 0 || idx >= LayerStack_Count()) break;
            LayerStack_DeleteLayer(idx);
            if (appState->activeLayer >= LayerStack_Count())
                appState->activeLayer = LayerStack_Count() - 1;
            layersDirty = true;
            break;
        }
        case laDup: {
            int idx = lact.layer;
            if (idx < 0 || idx >= LayerStack_Count()) break;
            LayerStack_DuplicateLayer(idx);
            layersDirty = true;
            break;
        }
        case laMove: {
            int fromIdx = lact.layer;
            int toIdx   = lact.layerto;
            if (fromIdx < 0 || fromIdx >= LayerStack_Count()) break;
            if (toIdx   < 0 || toIdx   >= LayerStack_Count()) break;
            if (fromIdx == toIdx) break;
            LayerStack_MoveLayer(fromIdx, toIdx);
            if (appState->activeLayer == fromIdx)
                appState->activeLayer = toIdx;
            layersDirty = true;
            break;
        }
        case laDrop: {
            int idx = lact.layer;
            if (idx <= 0 || idx >= LayerStack_Count()) break;
            MergeDownLayer(appState, idx);
            if (appState->activeLayer >= LayerStack_Count())
                appState->activeLayer = LayerStack_Count() - 1;
            layersDirty = true;
            break;
        }
        case laOp: {
            int idx = lact.layer;
            if (idx < 0 || idx >= LayerStack_Count()) break;
            LayerStack_GetProps(idx)->op = lact.op;
            layersDirty = true;
            break;
        }
        case laBm: {
            int idx = lact.layer;
            if (idx < 0 || idx >= LayerStack_Count()) break;
            LayerStack_GetProps(idx)->blendmode = lact.bm;
            layersDirty = true;
            break;
        }
        default: break;
        }
        break;
    }

    case sdFAIL:
        snprintf(statusMsg, sizeof(statusMsg), "Server rejected connection");
        Disconnect();
        break;

    default:
        break;
    }
}

void NetworkBroker::EnqueueRemoteDab(const d_Action* act) {
    if (!appState) return;
    int layer = act->layer;
    if (layer < 0 || layer >= LayerStack_Count()) return;
    if (LayerStack_GetRT(layer).id == 0) return;

    d_Brush brush = act->Brush;
    Vector2 pos1  = act->Stroke.pos1;
    Vector2 pos2  = act->Stroke.pos2;
    RenderTexture2D rt = LayerStack_GetRT(layer);
    if (rt.id > 0)
        BrushBlend_ApplyStamp(rt, &brush, g_activeBrushTex, pos1.x, pos1.y, pos2.x, pos2.y);
}

/* ── Config ─────────────────────────────────────────────────────────────── */

void NetworkBroker::LoadConfig(const char* path) {
    strncpy(configPath, path, sizeof(configPath) - 1);
    configPath[sizeof(configPath) - 1] = '\0';

    AppConfig cfg;
    AppConfig_Load(&cfg, path);

    if (cfg.lastServer[0])
        strncpy(serverAddr, cfg.lastServer,   sizeof(serverAddr)  - 1);
    if (cfg.lastUsername[0])
        strncpy(username,   cfg.lastUsername, sizeof(username)    - 1);
}

void NetworkBroker::SaveConfig() {
    AppConfig cfg;
    strncpy(cfg.lastServer,   serverAddr, sizeof(cfg.lastServer)   - 1);
    strncpy(cfg.lastUsername, username,   sizeof(cfg.lastUsername) - 1);

    if (configPath[0] && AppConfig_Save(&cfg, configPath)) return;
    if (AppConfig_Save(&cfg, "repaint.ini")) return;
    const char* ad = GetApplicationDirectory();
    if (ad && ad[0]) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%srepaint.ini", ad);
        AppConfig_Save(&cfg, buf);
    }
}

/* ── Connection UI ──────────────────────────────────────────────────────── */

void NetworkBroker::DrawConnectionUI(void) {
    if (!showUI) return;

    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network", &showUI)) { ImGui::End(); return; }

    ImGui::Text("Status: %s", statusMsg);
    ImGui::Separator();

    if (state == NS_DISCONNECTED) {
        ImGui::InputText("Server",   serverAddr, sizeof(serverAddr));
        ImGui::InputInt ("Port",    &serverPort);
        ImGui::InputText("Username", username,   sizeof(username));
        ImGui::TextDisabled("Password not required (local test server)");
        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            if (serverAddr[0] && username[0]) {
                SaveConfig();
                Connect(serverAddr, serverPort);
            }
        }
    } else {
        if (ImGui::Button("Disconnect", ImVec2(120, 0)))
            Disconnect();
    }

    ImGui::End();
}

/* ── sAuth serialization ────────────────────────────────────────────────── */

size_t sAuth::Serialize(uint8_t* buf, size_t cap) const {
    size_t off  = 0;
    size_t ulen = strlen(uname);
    size_t plen = strlen(upass);
    if (cap < 1 + 2 + ulen + 2 + plen) return 0;
    buf[off++] = aType;
    uint16_t un = (uint16_t)(ulen > 65535 ? 65535 : ulen);
    buf[off++] = (uint8_t)(un >> 8);
    buf[off++] = (uint8_t)(un);
    memcpy(buf + off, uname, un); off += un;
    uint16_t pn = (uint16_t)(plen > 65535 ? 65535 : plen);
    buf[off++] = (uint8_t)(pn >> 8);
    buf[off++] = (uint8_t)(pn);
    memcpy(buf + off, upass, pn); off += pn;
    return off;
}

bool sAuth::Deserialize(const uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + 1 > len) return false;
    aType = buf[off++];
    size_t used = SZstring_unpack(buf + off, len - off, uname, sizeof(uname));
    off += used;
    SZstring_unpack(buf + off, len - off, upass, sizeof(upass));
    return true;
}

/* ── stUserState serialization ──────────────────────────────────────────── */

size_t stUserState::Serialize(uint8_t* buf, size_t cap) const {
    size_t off = 0;
    if (cap < 1) return 0;
    buf[off++] = Ustate;
    size_t sz = SZstring(Uname, buf + off, cap - off);
    return off + sz;
}

bool stUserState::Deserialize(const uint8_t* buf, size_t len) {
    if (len < 1) return false;
    Ustate = buf[0];
    SZstring_unpack(buf + 1, len - 1, Uname, sizeof(Uname));
    return true;
}
