#include "network_broker.h"
#include "app_config.h"
#include "imgui.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static uint32_t PackColorNet(Color c) {
    return (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | (uint32_t)c.b | (uint32_t)c.a << 24;
}
static Color UnpackColorNet(uint32_t p) {
    Color c;
    c.r = (uint8_t)(p >> 16);
    c.g = (uint8_t)(p >> 8);
    c.b = (uint8_t)(p);
    c.a = (uint8_t)(p >> 24);
    return c;
}

static bool recv_all(int fd, uint8_t* buf, size_t len) {
    while (len > 0) {
        ssize_t n = recv(fd, buf, len, 0);
        if (n <= 0) return false;
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_all(int fd, const uint8_t* buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_DONTWAIT);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            return false;
        }
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

// ── sAuth serialization ──────────────────────────────────────────────────

size_t sAuth::Serialize(uint8_t* buf, size_t cap) const {
    size_t off = 0;
    size_t ulen = strlen(uname);
    size_t plen = strlen(upass);
    if (cap < 1 + 2 + ulen + 2 + plen) return 0;
    buf[off++] = aType;
    // uname as SZstring
    uint16_t un = (uint16_t)(ulen > 65535 ? 65535 : ulen);
    buf[off++] = (uint8_t)(un >> 8);
    buf[off++] = (uint8_t)(un);
    memcpy(buf + off, uname, un); off += un;
    // upass as SZstring
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
    used = SZstring_unpack(buf + off, len - off, upass, sizeof(upass));
    return true;
}

// ── stUserState serialization ────────────────────────────────────────────

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

// ── NetworkBroker ────────────────────────────────────────────────────────

NetworkBroker::NetworkBroker() {
    state = NS_DISCONNECTED;
    sockfd = -1;
    appState = NULL;
    configPath[0] = '\0';
    strcpy(serverAddr, "127.0.0.1");
    serverPort = NET_PORT;
    ownName[0] = '\0';
    userCount = 0;
    statusMsg[0] = '\0';
    showUI = false;
    localHead = 0;
    localTail = 0;
    recvThread = NULL;
    threadRunning = false;
    recvPos = 0;
    recvNeed = NET_HEADER_SIZE;

    // default username: "User" + random number
    unsigned int r = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)this;
    r = r * 1103515245 + 12345;
    snprintf(username, sizeof(username), "User%04u", (r / 65536) % 10000);
}

NetworkBroker::~NetworkBroker() {
    Disconnect();
}

void NetworkBroker::RecvThreadFunc(NetworkBroker* self) {
    uint8_t headerBuf[NET_HEADER_SIZE];

    while (self->threadRunning) {
        if (!recv_all(self->sockfd, headerBuf, NET_HEADER_SIZE)) {
            self->threadRunning = false;
            break;
        }

        stNetHead head;
        head.Deserialize(headerBuf);

        if (head.Hsize > RECV_BUF_SIZE - NET_HEADER_SIZE) {
            // bogus packet, disconnect
            self->threadRunning = false;
            break;
        }

        uint8_t* payload = (uint8_t*)malloc(head.Hsize + 1);
        if (!payload) break;
        payload[head.Hsize] = '\0';

        if (head.Hsize > 0) {
            if (!recv_all(self->sockfd, payload, head.Hsize)) {
                free(payload);
                self->threadRunning = false;
                break;
            }
        }

        ReceivedPacket pkt;
        pkt.hid = head.Hid;
        pkt.data = payload;
        pkt.size = head.Hsize;

        {
            std::lock_guard<std::mutex> lock(self->pktMtx);
            self->pktQueue.push(pkt);
        }
    }

    // clean up any remaining packets on exit
    std::lock_guard<std::mutex> lock(self->pktMtx);
    while (!self->pktQueue.empty()) {
        free(self->pktQueue.front().data);
        self->pktQueue.pop();
    }
}

// ── Connect / Disconnect ─────────────────────────────────────────────────

bool NetworkBroker::Connect(const char* addr, int port) {
    if (state != NS_DISCONNECTED) return false;

    snprintf(statusMsg, sizeof(statusMsg), "Connecting to %s:%d...", addr, port);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(statusMsg, sizeof(statusMsg), "Failed to create socket");
        return false;
    }

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &srv.sin_addr) <= 0) {
        snprintf(statusMsg, sizeof(statusMsg), "Invalid address: %s", addr);
        close(sockfd);
        sockfd = -1;
        return false;
    }

    if (connect(sockfd, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        snprintf(statusMsg, sizeof(statusMsg), "Connection failed: %s", strerror(errno));
        close(sockfd);
        sockfd = -1;
        return false;
    }

    strncpy(serverAddr, addr, sizeof(serverAddr) - 1);
    serverPort = port;
    state = NS_CONNECTING;
    threadRunning = true;
    recvThread = new std::thread(RecvThreadFunc, this);

    // send auth (password not used on local test server)
    sAuth auth;
    strncpy(auth.uname, username, sizeof(auth.uname) - 1);
    auth.upass[0] = '\0';
    auth.aType = atLogin;

    uint8_t authBuf[1024];
    size_t authSz = auth.Serialize(authBuf, sizeof(authBuf));
    SendPacket(sdAuth, authBuf, (uint32_t)authSz);
    state = NS_AUTH_SENT;

    snprintf(statusMsg, sizeof(statusMsg), "Authenticating as %s...", username);
    return true;
}

void NetworkBroker::Disconnect() {
    if (state == NS_DISCONNECTED) return;

    threadRunning = false;
    if (sockfd >= 0) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }
    if (recvThread && recvThread->joinable()) {
        recvThread->join();
        delete recvThread;
        recvThread = NULL;
    }

    // drain any remaining packets
    {
        std::lock_guard<std::mutex> lock(pktMtx);
        while (!pktQueue.empty()) {
            free(pktQueue.front().data);
            pktQueue.pop();
        }
    }

    state = NS_DISCONNECTED;
    ownName[0] = '\0';
    userCount = 0;
    snprintf(statusMsg, sizeof(statusMsg), "Disconnected");
}

// ── Send ─────────────────────────────────────────────────────────────────

void NetworkBroker::SendPacket(uint8_t hid, const uint8_t* data, uint32_t size) {
    if (sockfd < 0) return;

    stNetHead head;
    head.Hid = hid;
    head.Hsize = size;

    uint8_t headerBuf[NET_HEADER_SIZE];
    head.Serialize(headerBuf);

    if (!send_all(sockfd, headerBuf, NET_HEADER_SIZE)) {
        Disconnect();
        return;
    }
    if (size > 0) {
        if (!send_all(sockfd, data, size)) {
            Disconnect();
        }
    }
}

void NetworkBroker::SendAction(const d_Action* act) {
    uint8_t buf[4096];
    size_t sz = Action_Serialize((d_Action*)act, buf, sizeof(buf));
    if (sz > 0)
        SendPacket(sdAction, buf, (uint32_t)sz);
}

void NetworkBroker::SendLAction(const d_LAction* lact) {
    uint8_t buf[512];
    size_t sz = LAction_Serialize((d_LAction*)lact, buf, sizeof(buf));
    if (sz > 0)
        SendPacket(sdLAction, buf, (uint32_t)sz);
}

void NetworkBroker::SendChat(const char* msg) {
    uint8_t buf[2048];
    size_t sz = SZstring(msg, buf, sizeof(buf));
    SendPacket(sdGetMsg, buf, (uint32_t)sz);
}

// ── ICommandBroker ───────────────────────────────────────────────────────

void NetworkBroker::on_input(const InputEvent& e) {
    // queue local dab (same as LocalBroker)
    int next = (localTail + 1) % CMD_CAPACITY;
    if (next == localHead) return;
    if (!appState) return;

    d_Brush* br = &appState->currentBrush;
    int layer = appState->activeLayer;

    localQueue[localTail].x = e.x;
    localQueue[localTail].y = e.y;
    localQueue[localTail].color = Color{
        (uint8_t)((e.color >> 16) & 0xFF),
        (uint8_t)((e.color >> 8) & 0xFF),
        (uint8_t)(e.color & 0xFF),
        (uint8_t)((e.color >> 24) & 0xFF)
    };
    localQueue[localTail].rad_out = br->Realb.rad_out;
    localQueue[localTail].rad_in = br->Realb.rad_in;
    localQueue[localTail].opacity = br->Realb.opacity;
    localQueue[localTail].crv = br->Realb.crv;
    localQueue[localTail].x2y = br->Realb.x2y;
    localQueue[localTail].sol = br->Realb.sol;
    localQueue[localTail].sol2op = br->Realb.sol2op;
    localQueue[localTail].resangle = (float)br->Realb.resangle;
    localQueue[localTail].bmidx = (int)br->Realb.bmidx;
    localQueue[localTail].seed = br->Realb.seed;
    localQueue[localTail].activeLayer = layer;
    localQueue[localTail].targetRT = appState->layerRTs[layer];

    localTail = next;
}

void NetworkBroker::poll(AppState* state) {
    this->appState = state;

    // drain local queue — apply locally and relay to network
    while (localHead != localTail) {
        QueuedNetDab* d = &localQueue[localHead];
        bool applied = false;
        if (d->targetRT.id != 0 && d->activeLayer >= 0 && d->activeLayer < state->texCount) {
            d_Brush brush = {};
            brush.Realb.rad_in = d->rad_in;
            brush.Realb.rad_out = d->rad_out;
            brush.Realb.opacity = d->opacity;
            brush.Realb.crv = d->crv;
            brush.Realb.x2y = d->x2y;
            brush.Realb.sol = d->sol;
            brush.Realb.sol2op = d->sol2op;
            brush.Realb.resangle = d->resangle;
            brush.Realb.bmidx = (uint8_t)d->bmidx;
            brush.Realb.seed = d->seed;
            brush.Realb.col = d->color;

            RenderTexture2D rt = state->layerRTs[d->activeLayer];
            if (rt.id > 0) {
                BrushBlend_ApplyStamp(rt, &brush, d->x, d->y);
                applied = true;
            }
        }

        // relay to network (non-blocking send, drops silently if buffer full)
        if (applied && this->state == NS_CONNECTED) {
            d_Action act;
            act.ToolID = (uint8_t)state->mode;
            act.Brush = {};
            act.Brush.Realb.rad_in = d->rad_in;
            act.Brush.Realb.rad_out = d->rad_out;
            act.Brush.Realb.opacity = d->opacity;
            act.Brush.Realb.crv = d->crv;
            act.Brush.Realb.x2y = d->x2y;
            act.Brush.Realb.sol = d->sol;
            act.Brush.Realb.sol2op = d->sol2op;
            act.Brush.Realb.resangle = d->resangle;
            act.Brush.Realb.bmidx = (uint8_t)d->bmidx;
            act.Brush.Realb.seed = d->seed;
            act.Brush.Realb.col = d->color;
            act.startseed = 0;
            act.Noisemode = 0;
            act.Stroke.pos1 = Vector2{d->x, d->y};
            act.Stroke.pos2 = Vector2{d->x, d->y};
            act.layer = (uint8_t)d->activeLayer;
            SendAction(&act);
        }
        localHead = (localHead + 1) % CMD_CAPACITY;
    }

    // drain received packets from network
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

    // handle auth state transitions
    if (this->state == NS_AUTH_SENT) {
        // still waiting for sdLoginS — check if we timed out
        // status msg already set
    }
}

// ── Packet dispatch ──────────────────────────────────────────────────────

void NetworkBroker::ProcessReceived(uint8_t hid, uint8_t* data, uint32_t size) {
    switch (hid) {
    case sdLogin:
        snprintf(statusMsg, sizeof(statusMsg), "Server requested auth for %s", username);
        break;

    case sdLoginS: {
        char name[256] = "";
        SZstring_unpack(data, size, name, sizeof(name));
        state = NS_CONNECTED;
        strncpy(ownName, name, sizeof(ownName) - 1);
        strncpy(username, name, sizeof(username) - 1);
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
            EnqueueRemoteDab(&act);
        }
        break;
    }

    case sdUserStat: {
        stUserState us;
        us.Deserialize(data, size);
        // just log for now
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

    case sdGetMsg: {
        char msg[2048] = "";
        SZstring_unpack(data, size, msg, sizeof(msg));
        msg[sizeof(msg) - 1] = '\0';
        char shortmsg[200];
        strncpy(shortmsg, msg, sizeof(shortmsg) - 1);
        shortmsg[sizeof(shortmsg) - 1] = '\0';
        snprintf(statusMsg, sizeof(statusMsg), "Chat: %s", shortmsg);
        break;
    }

    case sdLAction: {
        if (!appState) break;
        d_LAction lact;
        if (!LAction_Deserialize(&lact, data, size)) break;

        switch (lact.ActID) {
        case laAdd: {
            int insertAfter = lact.layer;
            if (insertAfter < 0) insertAfter = 0;
            Canvas_InsertLayer(&appState->canvas, insertAfter);
            SyncAllRTs(appState);
            if (appState->activeLayer >= insertAfter)
                appState->activeLayer++;
            layersDirty = true;
            break;
        }
        case laDel: {
            int idx = lact.layer;
            if (idx < 0 || idx >= appState->canvas.layerCount) break;
            Canvas_DeleteLayer(&appState->canvas, idx);
            SyncAllRTs(appState);
            if (appState->activeLayer >= appState->canvas.layerCount)
                appState->activeLayer = appState->canvas.layerCount - 1;
            layersDirty = true;
            break;
        }
        case laDup: {
            int idx = lact.layer;
            if (idx < 0 || idx >= appState->canvas.layerCount) break;
            Canvas_DuplicateLayer(&appState->canvas, idx);
            SyncAllRTs(appState);
            layersDirty = true;
            break;
        }
        case laMove: {
            int fromIdx = lact.layer;
            int toIdx = lact.layerto;
            if (fromIdx < 0 || fromIdx >= appState->canvas.layerCount) break;
            if (toIdx < 0 || toIdx >= appState->canvas.layerCount) break;
            if (fromIdx == toIdx) break;
            Canvas_MoveLayer(&appState->canvas, fromIdx, toIdx);
            SyncAllRTs(appState);
            if (appState->activeLayer == fromIdx)
                appState->activeLayer = toIdx;
            layersDirty = true;
            break;
        }
        case laDrop: {
            int idx = lact.layer;
            if (idx <= 0 || idx >= appState->canvas.layerCount) break;
            Canvas_MergeDown(&appState->canvas, idx);
            SyncAllRTs(appState);
            if (appState->activeLayer >= appState->canvas.layerCount)
                appState->activeLayer = appState->canvas.layerCount - 1;
            layersDirty = true;
            break;
        }
        case laOp: {
            int idx = lact.layer;
            if (idx < 0 || idx >= appState->canvas.layerCount) break;
            Canvas_SetLayerOpacity(&appState->canvas, idx, lact.op);
            layersDirty = true;
            break;
        }
        case laBm: {
            int idx = lact.layer;
            if (idx < 0 || idx >= appState->canvas.layerCount) break;
            Canvas_SetLayerBlendMode(&appState->canvas, idx, lact.bm);
            layersDirty = true;
            break;
        }
        default:
            break;
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
    if (layer < 0 || layer >= appState->texCount) return;
    if (appState->layerRTs[layer].id == 0) return;

    d_Brush brush = act->Brush;
    // apply at stroke endpoints
    Vector2 pos1 = act->Stroke.pos1;
    RenderTexture2D rt = appState->layerRTs[layer];
    if (rt.id > 0) {
        BrushBlend_ApplyStamp(rt, &brush, pos1.x, pos1.y);
    }
}

// ── Config ───────────────────────────────────────────────────────────────

void NetworkBroker::LoadConfig(const char* path) {
    strncpy(configPath, path, sizeof(configPath) - 1);
    configPath[sizeof(configPath) - 1] = '\0';

    AppConfig cfg;
    AppConfig_Load(&cfg, path);

    if (cfg.lastServer[0])
        strncpy(serverAddr, cfg.lastServer, sizeof(serverAddr) - 1);
    if (cfg.lastUsername[0])
        strncpy(username, cfg.lastUsername, sizeof(username) - 1);
}

void NetworkBroker::SaveConfig() {
    AppConfig cfg;
    strncpy(cfg.lastServer, serverAddr, sizeof(cfg.lastServer) - 1);
    strncpy(cfg.lastUsername, username, sizeof(cfg.lastUsername) - 1);

    // try stored path; fallback to cwd; then try app directory
    if (configPath[0] && AppConfig_Save(&cfg, configPath)) return;
    if (AppConfig_Save(&cfg, "repaint.ini")) return;
    const char* ad = GetApplicationDirectory();
    if (ad && ad[0]) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%srepaint.ini", ad);
        AppConfig_Save(&cfg, buf);
    }
}

// ── Connection UI ────────────────────────────────────────────────────────

void NetworkBroker::DrawConnectionUI(void) {
    if (!showUI) return;

    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network", &showUI)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Status: %s", statusMsg);
    ImGui::Separator();

    if (state == NS_DISCONNECTED) {
        ImGui::InputText("Server", serverAddr, sizeof(serverAddr));
        ImGui::InputInt("Port", &serverPort);
        ImGui::InputText("Username", username, sizeof(username));
        ImGui::TextDisabled("Password not required (local test server)");

        if (ImGui::Button("Connect", ImVec2(120, 0))) {
            if (serverAddr[0] && username[0]) {
                SaveConfig();
                Connect(serverAddr, serverPort);
            }
        }
    } else {
        if (ImGui::Button("Disconnect", ImVec2(120, 0))) {
            Disconnect();
        }
    }

    ImGui::End();
}
