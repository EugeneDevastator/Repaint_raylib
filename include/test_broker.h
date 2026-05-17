#ifndef TEST_BROKER_H
#define TEST_BROKER_H

#include "repaint.h"

struct TestBroker : ICommandBroker {
    static const int CMD_CAPACITY = 4096;

    struct Dab {
        float x, y;
        float srcX, srcY;
        d_RealBrush brush;
        int activeLayer;
    };

    Dab queue[CMD_CAPACITY];
    volatile int head;
    volatile int tail;

    TestBroker();
    AppState* appState;
    void on_input(const InputEvent& e) override;
    void poll(AppState* state) override;
};

extern TestBroker g_testBroker;
extern bool g_useTestBroker;

#endif
