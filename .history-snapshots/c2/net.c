#include "uftp/net.h"

int uftp_net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        uftp_log("WSAStartup failed");
        return -1;
    }
#endif
    return 0;
}

void uftp_net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static int net_would_block(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

int uftp_sock_open(uftp_sock_t *sock, uint16_t port) {
    memset(sock, 0, sizeof(*sock));
    sock->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock->fd == UFTP_INVALID_SOCKET) {
        uftp_log("socket() failed");
        return -1;
    }

    int yes = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) == UFTP_SOCKET_ERROR) {
        uftp_log("bind() failed on port %u", port);
        uftp_sock_close(sock);
        return -1;
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock->fd, FIONBIO, &mode);
#else
    int flags = fcntl(sock->fd, F_GETFL, 0);
    fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK);
#endif

    return 0;
}

void uftp_sock_close(uftp_sock_t *sock) {
    if (sock->fd != UFTP_INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(sock->fd);
#else
        close(sock->fd);
#endif
        sock->fd = UFTP_INVALID_SOCKET;
    }
}

int uftp_sock_set_peer(uftp_sock_t *sock, const char *host, uint16_t port) {
    memset(&sock->peer, 0, sizeof(sock->peer));
    sock->peer.sin_family = AF_INET;
    sock->peer.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &sock->peer.sin_addr) != 1) {
        uftp_log("invalid host: %s", host);
        return -1;
    }
    sock->has_peer = 1;
    return 0;
}

int uftp_sock_send(uftp_sock_t *sock, const void *data, size_t len) {
    if (!sock->has_peer) {
        return -1;
    }
    int sent = (int)sendto(sock->fd, (const char *)data, (int)len, 0,
                           (struct sockaddr *)&sock->peer, sizeof(sock->peer));
    if (sent == UFTP_SOCKET_ERROR) {
        if (!net_would_block()) {
            uftp_log("sendto failed");
        }
        return -1;
    }
    return sent;
}

int uftp_sock_recv(uftp_sock_t *sock, void *buf, size_t cap, int timeout_ms,
                   struct sockaddr_in *from) {
    uint64_t deadline = uftp_now_ms() + (uint64_t)timeout_ms;

    for (;;) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int n = (int)recvfrom(sock->fd, (char *)buf, (int)cap, 0,
                              (struct sockaddr *)&src, &slen);

        if (n >= 0) {
            if (from) {
                *from = src;
            }
            if (!sock->has_peer) {
                sock->peer = src;
                sock->has_peer = 1;
            }
            return n;
        }

        if (!net_would_block()) {
            return -1;
        }

        if (timeout_ms <= 0 || uftp_now_ms() >= deadline) {
            return 0;
        }

#ifdef _WIN32
        Sleep(1);
#else
        struct timespec ts = {0, 1000000L};
        nanosleep(&ts, NULL);
#endif
    }
}
