#ifndef UFTP_NET_H
#define UFTP_NET_H

#include "common.h"

typedef struct {
    uftp_socket_t fd;
    struct sockaddr_in peer;
    int has_peer;
} uftp_sock_t;

int uftp_net_init(void);
void uftp_net_cleanup(void);
int uftp_sock_open(uftp_sock_t *sock, uint16_t port);
void uftp_sock_close(uftp_sock_t *sock);
int uftp_sock_set_peer(uftp_sock_t *sock, const char *host, uint16_t port);
int uftp_sock_send(uftp_sock_t *sock, const void *data, size_t len);
int uftp_sock_recv(uftp_sock_t *sock, void *buf, size_t cap, int timeout_ms,
                   struct sockaddr_in *from);

#endif
