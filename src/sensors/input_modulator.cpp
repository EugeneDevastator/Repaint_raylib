#include "input_modulator.h"
#include "tablet.h"
#include <math.h>

// ── Internal state ──────────────────────────────────────────────────
static struct {
    // Position tracking
    float prevWX = 0, prevWY = 0;
    bool  hasPrev = false;
    double lastTime = 0;

    // Velocity (EMA-low-pass)
    float velocity = 0;

    // Direction: weighted ring buffer
    struct { float dirX, dirY; float weight; } ring[16];
    int head = 0, tail = 0;

    // Computed direction — raw angle in radians
    float csDir  = 0.0f;
    float csIdir = 0.0f;
    bool  csIdirSet = false;

    // Tablet values (read from cache at Update time)
    float pressure = 1.0f;
    float rotation = 0.5f;
    float tiltX = 0.0f, tiltY = 0.0f;
} M;

// ── Init ────────────────────────────────────────────────────────────
void InputModulator_Init(void) {
    M = {};
    M.csDir = 0.0f;
    M.csIdir = 0.0f;
    M.pressure = 1.0f;
    M.rotation = 0.5f;
    M.velocity = 0;
}

// ── Update (called every frame with mouse world position) ───────────
void InputModulator_Update(float wx, float wy, double time) {
    // Tablet: read from cached state (polled by Tablet_UpdateModulators earlier)
    if (Tablet_IsOn()) {
        TabletState ts = Tablet_GetLastState();
        M.pressure = ts.pressure;
        M.rotation = ts.rotation;
        M.tiltX    = ts.tiltX;
        M.tiltY    = ts.tiltY;
    } else {
        M.pressure = 1.0f;
        M.rotation = 0.5f;
        M.tiltX = 0.0f; M.tiltY = 0.0f;
    }

    // Movement delta
    float dx = 0, dy = 0, len = 0;
    if (M.hasPrev) {
        dx = wx - M.prevWX;
        dy = wy - M.prevWY;
        len = sqrtf(dx*dx + dy*dy);

        // Velocity: raw = len/20, clamp [0,1], EMA with α = 1-exp(-dt/0.3)
        double dt = fmax(time - M.lastTime, 0.001);
        float rawVel = fminf(len / 20.0f, 1.0f);
        float alpha = 1.0f - (float)exp(-dt / 0.3);
        M.velocity = M.velocity + alpha * (rawVel - M.velocity);

        // Push direction sample to ring buffer
        if (len > 0.5f) {
            float w = fminf(1.0f, len * 2.0f);
            int t = M.tail;
            M.ring[t].dirX = dx / len;
            M.ring[t].dirY = dy / len;
            M.ring[t].weight = w;
            M.tail = (t + 1) % 16;
            if (M.tail == M.head) M.head = (M.head + 1) % 16;
            M.csIdirSet = false;
        }
    }

    // Compute weighted average direction from ring buffer
    float sx = 0, sy = 0, tw = 0;
    int i = M.head;
    while (i != M.tail) {
        sx += M.ring[i].dirX * M.ring[i].weight;
        sy += M.ring[i].dirY * M.ring[i].weight;
        tw += M.ring[i].weight;
        i = (i + 1) % 16;
    }
    if (tw > 0.001f) {
        M.csDir = atan2f(sy / tw, sx / tw);
        if (!M.csIdirSet) {
            M.csIdir = M.csDir;
            M.csIdirSet = true;
        }
    }

    M.prevWX = wx;
    M.prevWY = wy;
    M.lastTime = time;
    M.hasPrev = true;
}

// ── Root snapshot ──────────────────────────────────────────────────
RootModulators InputModulator_GetRootSnapshot(void) {
    RootModulators r = {};
    r.pressure = M.pressure;
    r.rotation = M.rotation;
    r.tiltX    = M.tiltX;
    r.tiltY    = M.tiltY;
    r.velocity = M.velocity;
    r.dirX     = cosf(M.csDir);
    r.dirY     = sinf(M.csDir);
    return r;
}

// ── Full snapshot (root + computed + derived defaults) ─────────────
void InputModulator_GetAllSnapshot(ModulatorTable* out) {
    memset(out->val, 0, sizeof(float) * 25);
    out->val[csNone]     = 1.0f;
    out->val[csPressure] = M.pressure;
    out->val[csVel]      = M.velocity;
    out->val[csDir]      = (M.csDir + (float)M_PI) / (float)(M_PI * 2.0f);
    out->val[csRot]      = M.rotation;
    out->val[csTilt]     = sqrtf(M.tiltX*M.tiltX + M.tiltY*M.tiltY);
    out->val[csRelang]   = 0.5f;
    out->val[csHtilt]    = M.tiltX;
    out->val[csVtilt]    = M.tiltY;
    out->val[csLenpx]    = 1.0f;
    out->val[csAcc]      = 1.0f;
    out->val[csXtilt]    = M.tiltX;
    out->val[csYtilt]    = M.tiltY;
    out->val[csCrv]      = 0.5f;
    out->val[csIdir]     = (M.csIdir + (float)M_PI) / (float)(M_PI * 2.0f);
    out->val[csHVdir]    = 0.5f;
    out->val[csHVrot]    = 0.5f;
}
