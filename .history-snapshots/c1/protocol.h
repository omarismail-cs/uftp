#ifndef UFTP_PROTOCOL_H
#define UFTP_PROTOCOL_H

#include "common.h"

typedef enum {
    UFTP_MSG_HELLO     = 1,
    UFTP_MSG_HELLO_ACK = 2,
    UFTP_MSG_DATA      = 3,
    UFTP_MSG_ACK       = 4,
    UFTP_MSG_NACK      = 5,
    UFTP_MSG_FIN       = 6,
    UFTP_MSG_FIN_ACK   = 7,
} uftp_msg_type;

typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  type;
    uint16_t flags;
    uint32_t session_id;
    uint32_t seq;
    uint32_t cum_ack;
    uint64_t sack_bitmap;
    uint32_t total_size;
    uint16_t payload_len;
    uint16_t window;
    uint32_t hdr_crc;
} uftp_hdr_t;

#define UFTP_HDR_SIZE ((uint16_t)sizeof(uftp_hdr_t))
#define UFTP_MAX_PACKET (UFTP_HDR_SIZE + UFTP_MSS_MAX)

typedef struct {
    uftp_hdr_t hdr;
    uint8_t payload[UFTP_MSS_MAX];
} uftp_packet_t;

#endif
