#ifndef UFTP_COMMON_H
#define UFTP_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET uftp_socket_t;
#define UFTP_INVALID_SOCKET INVALID_SOCKET
#define UFTP_SOCKET_ERROR SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int uftp_socket_t;
#define UFTP_INVALID_SOCKET (-1)
#define UFTP_SOCKET_ERROR (-1)
#endif

#define UFTP_MAGIC_0 'U'
#define UFTP_MAGIC_1 'F'
#define UFTP_MAGIC_2 'T'
#define UFTP_MAGIC_3 'P'
#define UFTP_VERSION 1

#define UFTP_WINDOW_MAX 64
#define UFTP_SACK_BITS 64
#define UFTP_MSS_MAX 1400
#define UFTP_MAX_FILENAME 256

#define UFTP_INITIAL_RTO_MS 100
#define UFTP_MAX_RTO_MS 2000
#define UFTP_MAX_RETRIES 20

uint64_t uftp_now_ms(void);
void uftp_log(const char *fmt, ...);

#endif
