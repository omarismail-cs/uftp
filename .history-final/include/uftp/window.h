#ifndef UFTP_WINDOW_H
#define UFTP_WINDOW_H

#include "protocol.h"

typedef struct {
    uint32_t seq;
    uint16_t len;
    uint8_t data[UFTP_MSS_MAX];
    int valid;
    uint64_t sent_at_ms;
    int retries;
} uftp_send_slot_t;

typedef struct {
    uint32_t seq;
    uint16_t len;
    uint8_t data[UFTP_MSS_MAX];
    int valid;
} uftp_recv_slot_t;

typedef struct {
    uint32_t send_base;
    uint32_t send_next;
    uint16_t window;
    uftp_send_slot_t slots[UFTP_WINDOW_MAX];
} uftp_send_window_t;

typedef struct {
    uint32_t recv_base;
    uint16_t window;
    uftp_recv_slot_t slots[UFTP_WINDOW_MAX];
} uftp_recv_window_t;

void uftp_send_win_init(uftp_send_window_t *w, uint16_t window);
void uftp_recv_win_init(uftp_recv_window_t *w, uint16_t window);

int uftp_send_win_can_send(const uftp_send_window_t *w);
int uftp_send_win_in_flight(const uftp_send_window_t *w);
uftp_send_slot_t *uftp_send_win_slot(uftp_send_window_t *w, uint32_t seq);
int uftp_send_win_mark_acked(uftp_send_window_t *w, uint32_t seq);
void uftp_send_win_advance(uftp_send_window_t *w);
uftp_send_slot_t *uftp_send_win_oldest_unacked(uftp_send_window_t *w);

int uftp_recv_win_store(uftp_recv_window_t *w, uint32_t seq,
                        const uint8_t *data, uint16_t len);
int uftp_recv_win_has(uftp_recv_window_t *w, uint32_t seq);
uint64_t uftp_recv_win_sack_bitmap(const uftp_recv_window_t *w);
int uftp_recv_win_pop_inorder(uftp_recv_window_t *w, uint8_t *out,
                              uint16_t *out_len, uint32_t *out_seq);

#endif
