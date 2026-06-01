#ifndef LOCALPLAYER_INPUT_QUEUE_H
#define LOCALPLAYER_INPUT_QUEUE_H

#include "tablet.h"

struct InputPoint {
    float x, y;
    float pressure, tiltX, tiltY, rotation;
    float velocity;
    double timestamp;
};

class InputQueue {
public:
    InputQueue();
    void Clear();
    void AddPoint(const InputPoint& pt);
    int  Drain(InputPoint* out, int maxOut);
    bool IsEmpty() const { return m_head == m_tail; }

private:
    static const int CAPACITY = 1024;
    InputPoint m_buf[CAPACITY];
    int m_head;
    int m_tail;
};

extern InputQueue g_inputQueue;

#endif
