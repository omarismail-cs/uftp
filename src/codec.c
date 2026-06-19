#include "uftp/codec.h"

void uftp_hdr_init(uftp_hdr_t *hdr, uftp_msg_type type, uint32_t session_id) {
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic[0] = UFTP_MAGIC_0;
    hdr->magic[1] = UFTP_MAGIC_1;
    hdr->magic[2] = UFTP_MAGIC_2;
    hdr->magic[3] = UFTP_MAGIC_3;
    hdr->version = UFTP_VERSION;
    hdr->type = (uint8_t)type;
    hdr->session_id = session_id;
}

static uint32_t hdr_crc_compute(const uftp_hdr_t *hdr) {
    uftp_hdr_t tmp = *hdr;
    tmp.hdr_crc = 0;
    return uftp_crc32(&tmp, sizeof(tmp));
}

int uftp_packet_encode(uftp_packet_t *pkt, size_t *out_len) {
    if (pkt->hdr.payload_len > UFTP_MSS_MAX) {
        return -1;
    }
    pkt->hdr.hdr_crc = hdr_crc_compute(&pkt->hdr);
    size_t total = UFTP_HDR_SIZE + pkt->hdr.payload_len;
    if (out_len) {
        *out_len = total;
    }
    return 0;
}

int uftp_packet_decode(const uint8_t *buf, size_t len, uftp_packet_t *pkt) {
    if (len < UFTP_HDR_SIZE) {
        return -1;
    }
    memcpy(pkt, buf, UFTP_HDR_SIZE);
    if (pkt->hdr.magic[0] != UFTP_MAGIC_0 || pkt->hdr.magic[1] != UFTP_MAGIC_1 ||
        pkt->hdr.magic[2] != UFTP_MAGIC_2 || pkt->hdr.magic[3] != UFTP_MAGIC_3) {
        return -1;
    }
    if (pkt->hdr.version != UFTP_VERSION) {
        return -1;
    }
    if (pkt->hdr.payload_len > UFTP_MSS_MAX || len < UFTP_HDR_SIZE + pkt->hdr.payload_len) {
        return -1;
    }
    uint32_t expect = hdr_crc_compute(&pkt->hdr);
    if (expect != pkt->hdr.hdr_crc) {
        return -1;
    }
    if (pkt->hdr.payload_len > 0) {
        memcpy(pkt->payload, buf + UFTP_HDR_SIZE, pkt->hdr.payload_len);
    }
    return 0;
}
