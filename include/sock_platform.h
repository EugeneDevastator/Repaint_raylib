#ifndef SOCK_PLATFORM_H
#define SOCK_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* Opaque handle - no platform types leak out */
typedef uintptr_t sock_t;
#define SOCK_INVALID ((sock_t)(~0ULL))

#ifdef __cplusplus
extern "C" {
#endif

int    sock_init(void);
void   sock_shutdown(void);

sock_t sock_connect(const char* addr, int port);
void   sock_close(sock_t s);
void   sock_disconnect(sock_t s);

int    sock_send_all(sock_t s, const uint8_t* buf, size_t len);
int    sock_recv_all(sock_t s, uint8_t* buf, size_t len);

const char* sock_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
