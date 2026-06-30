#include "sd_client.h"
#include "sock_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static int write32be(uint8_t* buf, uint32_t v) {
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)(v);
    return 4;
}

static uint32_t read32be(const uint8_t* buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           (uint32_t)buf[3];
}

int sd_request(const char* prompt, float strength, float cfg, int steps,
               int w, int h,
               uint8_t** out_png, size_t* out_size,
               SDProgressFn progress_fn) {
    if (!prompt || !*prompt) return -1;

    sock_t s = sock_connect("127.0.0.1", 8000);
    if (s == SOCK_INVALID) return -1;

    int ok = -1;
    char header[2048];
    uint8_t hdr[4];
    uint8_t zero[4] = {0, 0, 0, 0};
    int n;

    // Send 'G' mode byte
    uint8_t mode = 'G';
    if (!sock_send_all(s, &mode, 1)) goto cleanup;

    // Build and send text header
    n = snprintf(header, sizeof(header),
        "prompt=%s\n"
        "steps=%d\n"
        "cfg=%.2f\n"
        "strength=%.2f\n"
        "width=%d\n"
        "height=%d\n"
        "\n",
        prompt, steps, cfg, strength, w, h);
    if (!sock_send_all(s, (uint8_t*)header, (size_t)n)) goto cleanup;

    // Send empty source PNG blob (txt2img — no source image)
    if (!sock_send_all(s, zero, 4)) goto cleanup;

    // Receive loop: type byte + 4B length + data
    while (1) {
        uint8_t type;
        if (!sock_recv_all(s, &type, 1)) goto cleanup;

        if (!sock_recv_all(s, hdr, 4)) goto cleanup;
        uint32_t len = read32be(hdr);
        if (len == 0) goto cleanup;

        if (type == 'P') {
            char* msg = (char*)malloc(len + 1);
            if (!msg) goto cleanup;
            if (!sock_recv_all(s, (uint8_t*)msg, len)) { free(msg); goto cleanup; }
            msg[len] = '\0';
            if (progress_fn) progress_fn(msg);
            free(msg);
        } else if (type == 'R') {
            uint8_t* resp = (uint8_t*)malloc(len);
            if (!resp) goto cleanup;
            if (!sock_recv_all(s, resp, len)) { free(resp); goto cleanup; }
            *out_png  = resp;
            *out_size = len;
            ok = 0;
            break;
        } else {
            goto cleanup;
        }
    }

cleanup:
    sock_close(s);
    return ok;
}
