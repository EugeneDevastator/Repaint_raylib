#include "repaint.h"
#include <math.h>

StrokePoint InputFilter::Feed(float x, float y, double time) {
    ring[tail % RING_SIZE].x = x;
    ring[tail % RING_SIZE].y = y;
    ring[tail % RING_SIZE].t = time;
    tail++;
    if (tail - head > RING_SIZE) head = tail - RING_SIZE;

    int count = tail - head;
    if (count >= 2) {
        int prev = (tail - 2) % RING_SIZE;
        float dx = x - ring[prev].x;
        float dy = y - ring[prev].y;
        float dist = sqrtf(dx * dx + dy * dy);
        float rawVel = fminf(dist / 20.0f, 1.0f);
        // First-order low-pass: τ = 1.5s, alpha varies with actual dt
        float dt = fmaxf((float)(time - ring[prev].t), 0.001f);
        float alpha = 1.0f - expf(-dt / 0.3f);
        smoothedVel += alpha * (rawVel - smoothedVel);
    }

    return {x, y, smoothedVel, 0.5f};
}
