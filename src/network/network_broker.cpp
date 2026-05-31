#include "network_broker.h"
#include "layerstack.h"
#include "serialize.h"
#include "sock_platform.h"
#include "app_config.h"
#include "brush_draw.h"
#include "stroke_engine.h"
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
    segHead       = 0;
    segTail       = 0;
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

void NetworkBroker::on_segment(const DrawSegment& seg) {
    int next = (segTail + 1) % CMD_CAPACITY;
    if (next == segHead) return;
    if (!appState) return;

    int layer = appState->activeLayer;
    QueuedSegment& d = segQueue[segTail];
    d.pos1 = seg.pos1; d.pos2 = seg.pos2;
    d.ctrl0 = seg.ctrl0; d.ctrl3 = seg.ctrl3;
    d.brushFrom = seg.brushFrom;
    d.brushTo  = seg.brush;
    d.seed = seg.seed;
    d.tool = seg.tool;
    d.seamless = seg.seamless;
    d.smudgeSrcX = seg.smudgeSrcX;
    d.smudgeSrcY = seg.smudgeSrcY;
    d.activeLayer = layer;
    d.targetRT = LayerStack_GetRT(layer);
    segTail = next;
}

void NetworkBroker::poll(AppState* st) {
    this->appState = st;

    while (segHead != segTail) {
        QueuedSegment* d = &segQueue[segHead];
        if (d->targetRT.id != 0 && d->activeLayer >= 0 && d->activeLayer < LayerStack_Count()) {
            RenderTexture2D rt = LayerStack_GetRT(d->activeLayer);
            if (rt.id > 0) {
                DrawSegment dseg;
                memset(&dseg, 0, sizeof(dseg));
                dseg.pos1 = d->pos1; dseg.pos2 = d->pos2;
                dseg.ctrl0 = d->ctrl0; dseg.ctrl3 = d->ctrl3;
                dseg.brushFrom = d->brushFrom; dseg.brush = d->brushTo;
                dseg.seed = d->seed; dseg.tool = d->tool;
                dseg.seamless = d->seamless;
                dseg.smudgeSrcX = d->smudgeSrcX; dseg.smudgeSrcY = d->smudgeSrcY;
                dseg.Noisemode = 0;

                DrawOneSegment(dseg, rt);

                if (this->state == NS_CONNECTED)
                    SendSegment(*d);
            }
        }
        segHead = (segHead + 1) % CMD_CAPACITY;
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
        if (Action_Deserialize(&act, data, size)) {
            NetSegment ns;
            memset(&ns, 0, sizeof(ns));
            ns.pos1 = act.Stroke.pos1;
            ns.pos2 = act.Stroke.pos2;
            ns.ctrl0 = ns.pos1;
            ns.ctrl3 = ns.pos2;
            ns.brushFrom = CollapseBrushParams(act.Brush.Realb, 0.0f, act.ToolID);
            ns.brushTo = ns.brushFrom;
            ns.seed = act.Brush.Realb.seed;
            ns.toolID = act.ToolID;
            ns.seamless = 0;
            ns.smudgeSrcX = ns.pos1.x;
            ns.smudgeSrcY = ns.pos1.y;
            ns.layer = act.layer;
            EnqueueRemoteSegment(ns);
        }
        break;
    }

    case sdSegment: {
        if (!appState) break;
        NetSegment ns;
        if (Segment_Deserialize(&ns, data, size))
            EnqueueRemoteSegment(ns);
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

void NetworkBroker::SendSegment(const QueuedSegment& seg) {
    NetSegment ns;
    memset(&ns, 0, sizeof(ns));
    ns.pos1 = seg.pos1; ns.pos2 = seg.pos2;
    ns.ctrl0 = seg.ctrl0; ns.ctrl3 = seg.ctrl3;
    ns.brushFrom = seg.brushFrom;
    ns.brushTo  = seg.brushTo;
    ns.seed = seg.seed;
    ns.toolID = seg.tool;
    ns.seamless = seg.seamless;
    ns.smudgeSrcX = seg.smudgeSrcX;
    ns.smudgeSrcY = seg.smudgeSrcY;
    ns.layer = seg.activeLayer;
    uint8_t buf[4096];
    size_t sz = Segment_Serialize(ns, buf, sizeof(buf));
    if (sz > 0) SendPacket(sdSegment, buf, (uint32_t)sz);
}

void NetworkBroker::EnqueueRemoteSegment(const NetSegment& ns) {
    int next = (segTail + 1) % CMD_CAPACITY;
    if (next == segHead) return;
    QueuedSegment& d = segQueue[segTail];
    d.pos1 = ns.pos1; d.pos2 = ns.pos2;
    d.ctrl0 = ns.ctrl0; d.ctrl3 = ns.ctrl3;
    d.brushFrom = ns.brushFrom;
    d.brushTo  = ns.brushTo;
    d.seed = ns.seed;
    d.tool = ns.toolID;
    d.seamless = ns.seamless;
    d.smudgeSrcX = ns.smudgeSrcX;
    d.smudgeSrcY = ns.smudgeSrcY;
    d.activeLayer = ns.layer;
    d.targetRT = LayerStack_GetRT(ns.layer);
    segTail = next;
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
