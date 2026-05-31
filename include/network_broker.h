#ifndef NETWORK_BROKER_H
#define NETWORK_BROKER_H

#include "sock_platform.h"
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
    uint8_t  hid;
    uint8_t* data;
    uint32_t size;
};

struct QueuedSegment {
    RenderTexture2D targetRT;
    Vector2 pos1, pos2, ctrl0, ctrl3;
    CollapsedBrush brushFrom, brushTo;
    uint16_t seed;
    uint8_t  tool, seamless;
    float smudgeSrcX, smudgeSrcY;
    int activeLayer;
};

struct NetworkBroker : ICommandBroker {
    static const int CMD_CAPACITY  = 4096;
    static const int RECV_BUF_SIZE = 65536;

    NetState state;
    sock_t   sockfd;

    AppState* appState;
    char serverAddr[256];
    int  serverPort;
    char username[64];
    char statusMsg[2048];
    bool showUI;

    QueuedSegment segQueue[CMD_CAPACITY];
    volatile int segHead;
    volatile int segTail;

    std::thread* recvThread;
    bool         threadRunning;
    uint8_t      recvBuf[RECV_BUF_SIZE];
    uint32_t     recvPos;
    uint32_t     recvNeed;

    std::mutex              pktMtx;
    std::queue<ReceivedPacket> pktQueue;

    char ownName[64];
    char userNames[64][256];
    int  userCount;
    bool layersDirty;

    char configPath[1024];

    NetworkBroker();
    ~NetworkBroker();

    void on_segment(const DrawSegment& seg) override;
    void poll(AppState* state) override;

    bool Connect(const char* addr, int port);
    void Disconnect();
    bool IsConnected() const { return state == NS_CONNECTED; }

    void SendPacket(uint8_t hid, const uint8_t* data, uint32_t size);
    void SendAction(const d_Action* act);
    void SendSegment(const QueuedSegment& seg);
    void SendLAction(const d_LAction* lact);

    void LoadConfig(const char* path);
    void SaveConfig();
    void DrawConnectionUI(void);

private:
    static void RecvThreadFunc(NetworkBroker* self);
    void ProcessReceived(uint8_t hid, uint8_t* data, uint32_t size);
    void EnqueueRemoteSegment(const NetSegment& ns);
};

extern NetworkBroker networkBroker;

#endif
