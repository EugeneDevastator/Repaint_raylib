#include "upscale_client.h"
#include "sock_platform.h"
#include <stdlib.h>
#include <string.h>

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

int upscale_request(const uint8_t* src_png, size_t src_size,
                    uint8_t** out_png, size_t* out_size) {
    if (!src_png || src_size == 0) return -1;

    sock_t s = sock_connect("127.0.0.1", 8000);
    if (s == SOCK_INVALID) return -1;

    int ok = -1;
    uint8_t hdr[4];

    uint8_t mode = 'U';
    if (!sock_send_all(s, &mode, 1)) goto cleanup;

    write32be(hdr, (uint32_t)src_size);
    if (!sock_send_all(s, hdr, 4)) goto cleanup;
    if (!sock_send_all(s, src_png, src_size)) goto cleanup;

    while (1) {
        uint8_t type;
        if (!sock_recv_all(s, &type, 1)) goto cleanup;
        if (!sock_recv_all(s, hdr, 4)) goto cleanup;
        uint32_t len = read32be(hdr);
        if (len == 0) goto cleanup;

        if (type == 'R') {
            uint8_t* resp = (uint8_t*)malloc(len);
            if (!resp) goto cleanup;
            if (!sock_recv_all(s, resp, len)) { free(resp); goto cleanup; }
            *out_png  = resp;
            *out_size = len;
            ok = 0;
            break;
        } else {
            // skip progress messages for upscale
            uint8_t* dummy = (uint8_t*)malloc(len);
            if (dummy) { sock_recv_all(s, dummy, len); free(dummy); }
        }
    }

cleanup:
    sock_close(s);
    return ok;
}
