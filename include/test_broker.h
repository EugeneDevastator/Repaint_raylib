#ifndef TEST_BROKER_H
#define TEST_BROKER_H

#include "repaint.h"

struct TestBroker : ICommandBroker {
    static const int CMD_CAPACITY = 4096;

    struct Dab {
        float x, y;
        float srcX, srcY;
        d_RealBrush brush;
        uint8_t targetType;  // 0 = layer, 1 = texture
        uint8_t targetId;
    };

    Dab queue[CMD_CAPACITY];
    volatile int head;
    volatile int tail;

    TestBroker();
    AppState* appState;
    void on_segment(const DrawSegment& seg) override;
    void poll(AppState* state) override;
};

extern TestBroker g_testBroker;
extern bool g_useTestBroker;

#endif
