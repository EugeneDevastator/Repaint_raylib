#ifndef NETWORK_BROKER_H
#define NETWORK_BROKER_H

#include "repaint.h"
#include "net_protocol.h"
#include <thread>
#include <mutex>
#include <queue>

enum NetState {
    NS_DISCONNECTED,
    NS_CONNECTING,
    NS_AUTH_WAITING,
    NS_AUTH_SENT,
    NS_CONNECTED
};

struct ReceivedPacket {
    uint8_t hid;
    uint8_t* data;
    uint32_t size;
};

struct QueuedNetDab {
    RenderTexture2D targetRT;
    float x, y;
    float rad_in, rad_out, opacity, crv, x2y, sol, sol2op, resangle;
    Color color;
    int bmidx;
    uint16_t seed;
    int activeLayer;
};

struct NetworkBroker : ICommandBroker {
    static const int CMD_CAPACITY = 4096;
    static const int RECV_BUF_SIZE = 65536;

    NetState state;
    int sockfd;

    AppState* appState;
    char serverAddr[256];
    int serverPort;
    char username[64];
    char statusMsg[2048];
    bool showUI;

    // local dab queue (same as LocalBroker)
    QueuedNetDab localQueue[CMD_CAPACITY];
    volatile int localHead;
    volatile int localTail;

    // recv thread
    std::thread* recvThread;
    bool threadRunning;
    uint8_t recvBuf[RECV_BUF_SIZE];
    uint32_t recvPos;
    uint32_t recvNeed;

    // received packets (produced by recv thread, consumed by poll)
    std::mutex pktMtx;
    std::queue<ReceivedPacket> pktQueue;

    // user list (modified/read only from main thread via poll)
    char ownName[64];
    char userNames[64][256];
    int userCount;

    NetworkBroker();
    ~NetworkBroker();

    void on_input(const InputEvent& e) override;
    void poll(AppState* state) override;

    // networking
    bool Connect(const char* addr, int port);
    void Disconnect();
    bool IsConnected() const { return state == NS_CONNECTED; }

    void SendPacket(uint8_t hid, const uint8_t* data, uint32_t size);
    void SendAction(const d_Action* act);
    void SendLAction(const d_LAction* lact);
    void SendChat(const char* msg);

    // config path (set by LoadConfig, used by auto-save on connect)
    char configPath[1024];

    // config
    void LoadConfig(const char* path);
    void SaveConfig();

    // ui
    void DrawConnectionUI(void);

private:
    static void RecvThreadFunc(NetworkBroker* self);
    void ProcessReceived(uint8_t hid, uint8_t* data, uint32_t size);
    void EnqueueRemoteDab(const d_Action* act);
};

extern NetworkBroker networkBroker;

#endif
