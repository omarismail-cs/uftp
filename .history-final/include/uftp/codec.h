#ifndef UFTP_CODEC_H
#define UFTP_CODEC_H

#include "protocol.h"

void uftp_hdr_init(uftp_hdr_t *hdr, uftp_msg_type type, uint32_t session_id);
int uftp_packet_encode(uftp_packet_t *pkt, size_t *out_len);
int uftp_packet_decode(const uint8_t *buf, size_t len, uftp_packet_t *pkt);

#endif
