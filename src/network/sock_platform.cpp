/* Platform includes ONLY here, never in the header */
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
#endif

#include "sock_platform.h"
#include <string.h>
#include <stdio.h>

/* ── helpers to convert opaque handle ──────────────────────────────────── */

#define SOCK_BACKLOG 16

#ifdef _WIN32
  static SOCKET to_native(sock_t s) { return (SOCKET)s; }
  static sock_t from_native(SOCKET s) { return (sock_t)s; }
#else
  static int    to_native(sock_t s)  { return (int)s; }
  static sock_t from_native(int s)   { return (sock_t)s; }
#endif

/* ── Windows ────────────────────────────────────────────────────────────── */

#ifdef _WIN32

static char s_errbuf[128];

const char* sock_last_error(void) {
    snprintf(s_errbuf, sizeof(s_errbuf), "WSA error %d", WSAGetLastError());
    return s_errbuf;
}

int sock_init(void) {
    WSADATA wd;
    return WSAStartup(MAKEWORD(2,2), &wd) == 0 ? 1 : 0;
}

void sock_shutdown(void) { WSACleanup(); }

void sock_close(sock_t s) {
    if (s != SOCK_INVALID) closesocket(to_native(s));
}

void sock_disconnect(sock_t s) {
    if (s != SOCK_INVALID) {
        shutdown(to_native(s), SD_BOTH);
        closesocket(to_native(s));
    }
}

int sock_send_all(sock_t s, const uint8_t* buf, size_t len) {
    SOCKET ns = to_native(s);
    while (len > 0) {
        int n = send(ns, (const char*)buf, (int)len, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return 1;
            return 0;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 1;
}

int sock_recv_all(sock_t s, uint8_t* buf, size_t len) {
    SOCKET ns = to_native(s);
    while (len > 0) {
        int n = recv(ns, (char*)buf, (int)len, 0);
        if (n <= 0) return 0;
        buf += n;
        len -= (size_t)n;
    }
    return 1;
}

sock_t sock_connect(const char* addr, int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return SOCK_INVALID;

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &srv.sin_addr) <= 0) {
        closesocket(s); return SOCK_INVALID;
    }
    if (connect(s, (struct sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        closesocket(s); return SOCK_INVALID;
    }
    return from_native(s);
}

sock_t sock_listen(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return SOCK_INVALID;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s); return SOCK_INVALID;
    }
    if (listen(s, SOCK_BACKLOG) == SOCKET_ERROR) {
        closesocket(s); return SOCK_INVALID;
    }
    return from_native(s);
}

sock_t sock_accept(sock_t listener) {
    struct sockaddr_in cli;
    int cli_len = sizeof(cli);
    SOCKET s = accept(to_native(listener), (struct sockaddr*)&cli, &cli_len);
    if (s == INVALID_SOCKET) return SOCK_INVALID;
    return from_native(s);
}

/* ── POSIX ──────────────────────────────────────────────────────────────── */

#else

const char* sock_last_error(void) { return strerror(errno); }
int  sock_init(void)     { return 1; }
void sock_shutdown(void) {}

void sock_close(sock_t s) {
    if (s != SOCK_INVALID) close(to_native(s));
}

void sock_disconnect(sock_t s) {
    if (s != SOCK_INVALID) {
        shutdown(to_native(s), SHUT_RDWR);
        close(to_native(s));
    }
}

int sock_send_all(sock_t s, const uint8_t* buf, size_t len) {
    int ns = to_native(s);
    while (len > 0) {
        ssize_t n = send(ns, buf, len, MSG_DONTWAIT);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
            return 0;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 1;
}

int sock_recv_all(sock_t s, uint8_t* buf, size_t len) {
    int ns = to_native(s);
    while (len > 0) {
        ssize_t n = recv(ns, buf, len, 0);
        if (n <= 0) return 0;
        buf += n;
        len -= (size_t)n;
    }
    return 1;
}

sock_t sock_connect(const char* addr, int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return SOCK_INVALID;

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &srv.sin_addr) <= 0) {
        close(s); return SOCK_INVALID;
    }
    if (connect(s, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        close(s); return SOCK_INVALID;
    }
    return from_native(s);
}

sock_t sock_listen(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return SOCK_INVALID;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(s); return SOCK_INVALID;
    }
    if (listen(s, SOCK_BACKLOG) < 0) {
        close(s); return SOCK_INVALID;
    }
    return from_native(s);
}

sock_t sock_accept(sock_t listener) {
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int s = accept(to_native(listener), (struct sockaddr*)&cli, &cli_len);
    if (s < 0) return SOCK_INVALID;
    return from_native(s);
}

#endif
