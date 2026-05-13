#include "repaint.h"
#include <math.h>

StrokePoint InputFilter::Feed(float x, float y, double time) {
    ring[tail].x = x; ring[tail].y = y; ring[tail].t = time;
    int newTail = (tail + 1) % RING_SIZE;
    if (newTail == head) head = (head + 1) % RING_SIZE;
    tail = newTail;

    float vel = 0.0f;
    int count = (tail - head + RING_SIZE) % RING_SIZE;
    if (count >= 2) {
        int prev = (tail - 2 + RING_SIZE) % RING_SIZE;
        float dx = x - ring[prev].x;
        float dy = y - ring[prev].y;
        float dt = (float)(time - ring[prev].t);
        if (dt > 0.001f) {
            float dist = sqrtf(dx * dx + dy * dy);
            vel = fminf(dist / dt / 3000.0f, 1.0f);
        }
    }

    return {x, y, vel, 0.5f};
}
