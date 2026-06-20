#ifndef UFTP_STATS_H
#define UFTP_STATS_H

#include <stdint.h>

typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t retransmits;
    uint64_t acks_recv;
    uint64_t acks_sent;
    uint64_t gaps_detected;
    uint64_t start_ms;
} uftp_stats_t;

void uftp_stats_init(uftp_stats_t *s);
void uftp_stats_print(const uftp_stats_t *s);

#endif
