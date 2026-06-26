#ifndef LOCALPLAYER_INPUT_QUEUE_H
#define LOCALPLAYER_INPUT_QUEUE_H

#include "repaint.h"
#include "tablet.h"

struct InputEntry {
    enum Type : uint8_t { Begin, Point, End };
    Type type;

    // Point + Begin: position
    float x, y;
    float pressure, tiltX, tiltY, rotation;
    float velocity;
    double timestamp;

    // Begin only: stroke context
    d_RealBrush brush;
    float initAngle;
    int toolMode;
    TexSlotID targetSlot;
    uint8_t userTexBucket;  // TM_BUCKET_USER typically, 0xFF = none
    uint8_t userTexSlot;    // slot within bucket, 0xFF = none
    float worldToTexPx;
};

class InputQueue {
public:
    static const int CAPACITY = 2048;

    void AddEntry(const InputEntry& e);
    int  Drain(InputEntry* out, int maxOut);
    bool IsEmpty() const { return m_head == m_tail; }

private:
    InputEntry m_buf[CAPACITY];
    int m_head = 0;
    int m_tail = 0;
};

extern InputQueue g_inputQueue;

#endif
