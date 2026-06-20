#include "uftp/window.h"

static int seq_in_window(uint32_t base, uint32_t seq, uint16_t window) {
    uint32_t end = base + window;
    if (end >= base) {
        return seq >= base && seq < end;
    }
    return seq >= base || seq < end;
}

static int slot_index(uint32_t seq, uint16_t window) {
    return (int)(seq % window);
}

void uftp_send_win_init(uftp_send_window_t *w, uint16_t window) {
    memset(w, 0, sizeof(*w));
    w->window = window;
}

void uftp_recv_win_init(uftp_recv_window_t *w, uint16_t window) {
    memset(w, 0, sizeof(*w));
    w->window = window;
}

int uftp_send_win_can_send(const uftp_send_window_t *w) {
    return (w->send_next - w->send_base) < w->window;
}

int uftp_send_win_in_flight(const uftp_send_window_t *w) {
    return (int)(w->send_next - w->send_base);
}

uftp_send_slot_t *uftp_send_win_slot(uftp_send_window_t *w, uint32_t seq) {
    if (!seq_in_window(w->send_base, seq, w->window) &&
        seq != w->send_next) {
        return NULL;
    }
    uftp_send_slot_t *slot = &w->slots[slot_index(seq, w->window)];
    if (slot->valid && slot->seq != seq) {
        return NULL;
    }
    return slot;
}

int uftp_send_win_mark_acked(uftp_send_window_t *w, uint32_t seq) {
    if (!seq_in_window(w->send_base, seq, w->window * 2)) {
        return 0;
    }
    uftp_send_slot_t *slot = &w->slots[slot_index(seq, w->window)];
    if (slot->valid && slot->seq == seq) {
        slot->valid = 0;
        return 1;
    }
    return 0;
}

void uftp_send_win_advance(uftp_send_window_t *w) {
    while (w->send_base < w->send_next) {
        uftp_send_slot_t *slot = &w->slots[slot_index(w->send_base, w->window)];
        if (slot->valid && slot->seq == w->send_base) {
            break;
        }
        w->send_base++;
    }
}

uftp_send_slot_t *uftp_send_win_oldest_unacked(uftp_send_window_t *w) {
    for (uint32_t seq = w->send_base; seq < w->send_next; seq++) {
        uftp_send_slot_t *slot = &w->slots[slot_index(seq, w->window)];
        if (slot->valid && slot->seq == seq) {
            return slot;
        }
    }
    return NULL;
}

int uftp_recv_win_store(uftp_recv_window_t *w, uint32_t seq,
                        const uint8_t *data, uint16_t len) {
    if (!seq_in_window(w->recv_base, seq, w->window)) {
        return -1;
    }
    uftp_recv_slot_t *slot = &w->slots[slot_index(seq, w->window)];
    if (slot->valid && slot->seq == seq) {
        return 0;
    }
    slot->seq = seq;
    slot->len = len;
    memcpy(slot->data, data, len);
    slot->valid = 1;
    return 1;
}

int uftp_recv_win_has(uftp_recv_window_t *w, uint32_t seq) {
    uftp_recv_slot_t *slot = &w->slots[slot_index(seq, w->window)];
    return slot->valid && slot->seq == seq;
}

uint64_t uftp_recv_win_sack_bitmap(const uftp_recv_window_t *w) {
    uint64_t bitmap = 0;
    uint32_t sack_limit = w->window - 1;
    if (sack_limit > UFTP_SACK_BITS) {
        sack_limit = UFTP_SACK_BITS;
    }
    for (uint32_t i = 0; i < sack_limit; i++) {
        uint32_t seq = w->recv_base + 1 + i;
        if (!seq_in_window(w->recv_base, seq, w->window)) {
            break;
        }
        const uftp_recv_slot_t *slot = &w->slots[slot_index(seq, w->window)];
        if (slot->valid && slot->seq == seq) {
            bitmap |= (1ULL << i);
        }
    }
    return bitmap;
}

int uftp_recv_win_pop_inorder(uftp_recv_window_t *w, uint8_t *out,
                              uint16_t *out_len, uint32_t *out_seq) {
    uftp_recv_slot_t *slot = &w->slots[slot_index(w->recv_base, w->window)];
    if (!slot->valid || slot->seq != w->recv_base) {
        return 0;
    }
    memcpy(out, slot->data, slot->len);
    *out_len = slot->len;
    *out_seq = w->recv_base;
    slot->valid = 0;
    w->recv_base++;
    return 1;
}
