#include "repaint.h"
#include <math.h>

StrokePoint InputFilter::Feed(float x, float y, double time) {
    ring[tail % RING_SIZE].x = x;
    ring[tail % RING_SIZE].y = y;
    ring[tail % RING_SIZE].t = time;
    tail++;
    if (tail - head > RING_SIZE) head = tail - RING_SIZE;

    float rawVel = smoothedVel;  // hold last value if no measurement
    int count = tail - head;
    if (count >= 2) {
        int prev = (tail - 2) % RING_SIZE;
        float dx = x - ring[prev].x;
        float dy = y - ring[prev].y;
        float dist = sqrtf(dx * dx + dy * dy);
        // Qt: raw distance (no dt), maxvel=20px saturates to 1.0
        rawVel = fminf(dist / 20.0f, 1.0f);
    }

    // Qt: 50/50 blend between current and previous stroke pars
    const float alpha = 0.5f;
    smoothedVel = alpha * rawVel + (1.0f - alpha) * smoothedVel;

    return {x, y, smoothedVel, 0.5f};
}
