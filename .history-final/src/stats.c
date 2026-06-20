#include "uftp/stats.h"
#include "uftp/common.h"
#include <stdio.h>

void uftp_stats_init(uftp_stats_t *s) {
    memset(s, 0, sizeof(*s));
    s->start_ms = uftp_now_ms();
    s->crc_ok = -1;
}

void uftp_stats_print(const uftp_stats_t *s) {
    uint64_t elapsed = uftp_now_ms() - s->start_ms;
    if (elapsed == 0) {
        elapsed = 1;
    }
    double sec = (double)elapsed / 1000.0;
    double mbps = ((double)s->bytes_sent * 8.0) / (sec * 1000000.0);

    fprintf(stderr, "\n--- transfer stats ---\n");
    fprintf(stderr, "  duration:     %.2f s\n", sec);
    fprintf(stderr, "  bytes sent:   %llu\n", (unsigned long long)s->bytes_sent);
    fprintf(stderr, "  bytes recv:   %llu\n", (unsigned long long)s->bytes_recv);
    fprintf(stderr, "  packets sent: %llu\n", (unsigned long long)s->packets_sent);
    fprintf(stderr, "  packets recv: %llu\n", (unsigned long long)s->packets_recv);
    fprintf(stderr, "  retransmits:  %llu\n", (unsigned long long)s->retransmits);
    fprintf(stderr, "  acks recv:    %llu\n", (unsigned long long)s->acks_recv);
    fprintf(stderr, "  gaps:         %llu\n", (unsigned long long)s->gaps_detected);
    fprintf(stderr, "  throughput:   %.2f Mbps\n", mbps);
    if (s->crc_ok >= 0) {
        fprintf(stderr, "  file crc:     %08x\n", s->file_crc);
    }
    if (s->crc_ok == 1) {
        fprintf(stderr, "  verify:       OK\n");
    } else if (s->crc_ok == 0) {
        fprintf(stderr, "  verify:       FAILED\n");
    }
}
