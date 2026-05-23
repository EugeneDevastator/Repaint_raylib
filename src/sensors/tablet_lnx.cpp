#include "tablet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_DEVICES 64

static int g_penFds[MAX_DEVICES];
static int g_devCount = 0;

static int is_pen_device(int fd) {
    unsigned char bits[64] = {0};

    // Check for EV_KEY and EV_ABS
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), &bits) < 0) return 0;
    if (!(bits[BTN_TOOL_PEN / 8] & (1 << (BTN_TOOL_PEN % 8))) &&
        !(bits[BTN_TOUCH / 8] & (1 << (BTN_TOUCH % 8))))
        return 0;

    // Check for ABS_PRESSURE (pen has pressure sensitivity)
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), &bits) < 0) return 0;
    if (!(bits[ABS_PRESSURE / 8] & (1 << (ABS_PRESSURE % 8))))
        return 0;

    return 1;
}

int TabletPlatform_GetHookCount(void)   { return 0; }

void TabletPlatform_GetDebugInfo(char* buf, size_t sz) {
    snprintf(buf, sz, "Linux evdev — no Windows Ink equivalent");
}

bool TabletPlatform_Init(void* nativeWindow) {
    (void)nativeWindow;
    g_devCount = 0;

    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (is_pen_device(fd)) {
            if (g_devCount < MAX_DEVICES)
                g_penFds[g_devCount++] = fd;
            else
                close(fd);
        } else {
            close(fd);
        }
    }

    return (g_devCount > 0);
}

void TabletPlatform_Shutdown(void) {
    for (int i = 0; i < g_devCount; i++)
        close(g_penFds[i]);
    g_devCount = 0;
}

bool TabletPlatform_Poll(TabletState* state) {
    bool updated = false;

    for (int di = 0; di < g_devCount; di++) {
        struct input_event ev;

        while (read(g_penFds[di], &ev, sizeof(ev)) == (int)sizeof(ev)) {
            updated = true;

            switch (ev.type) {
            case EV_ABS:
                switch (ev.code) {
                case ABS_PRESSURE:
                    state->pressure = ev.value / 4096.0f;
                    if (state->pressure < 0.0f) state->pressure = 0.0f;
                    if (state->pressure > 1.0f) state->pressure = 1.0f;
                    break;
                case ABS_TILT_X:
                    state->tiltX = ev.value / 90.0f;
                    if (state->tiltX < -1.0f) state->tiltX = -1.0f;
                    if (state->tiltX > 1.0f) state->tiltX = 1.0f;
                    break;
                case ABS_TILT_Y:
                    state->tiltY = ev.value / 90.0f;
                    if (state->tiltY < -1.0f) state->tiltY = -1.0f;
                    if (state->tiltY > 1.0f) state->tiltY = 1.0f;
                    break;
                case ABS_Z:
                    state->rotation = ev.value / 360.0f;
                    break;
                case ABS_DISTANCE:
                    state->active = (ev.value < 10);
                    break;
                case ABS_X:
                case ABS_Y:
                    // position — not needed, raylib handles it
                    break;
                }
                break;
            case EV_KEY:
                switch (ev.code) {
                case BTN_TOUCH:
                    state->touching = (ev.value != 0);
                    break;
                case BTN_TOOL_PEN:
                    state->active = (ev.value != 0);
                    break;
                case BTN_STYLUS:
                case BTN_STYLUS2:
                    break;
                }
                break;
            }
        }
        // EAGAIN means no more events — normal for non-blocking
    }

    return updated;
}
