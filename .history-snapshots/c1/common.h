#ifndef UFTP_COMMON_H
#define UFTP_COMMON_H

#include <stdint.h>
#include <stddef.h>

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
typedef int uftp_socket_t;
#define UFTP_INVALID_SOCKET (-1)
#define UFTP_SOCKET_ERROR (-1)
#endif

#define UFTP_MAGIC_0 'U'
#define UFTP_MAGIC_1 'F'
#define UFTP_MAGIC_2 'T'
#define UFTP_MAGIC_3 'P'
#define UFTP_VERSION 1
#define UFTP_MSS_MAX 1400

#endif
