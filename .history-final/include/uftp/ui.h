#ifndef UFTP_UI_H
#define UFTP_UI_H

#include "stats.h"
#include "window.h"

#define UFTP_UI_LOG_LINES 6
#define UFTP_UI_SPARK_SAMPLES 40

typedef struct {
    int enabled;
    int active;
    uint64_t last_draw_ms;
    char role[16];
    char log[UFTP_UI_LOG_LINES][96];
    int log_count;
    double spark[UFTP_UI_SPARK_SAMPLES];
    int spark_head;
    uint64_t last_bytes;
    uint64_t last_sample_ms;
} uftp_ui_t;

void uftp_ui_init(uftp_ui_t *ui, int enabled, const char *role);
void uftp_ui_shutdown(uftp_ui_t *ui);
void uftp_ui_log(uftp_ui_t *ui, const char *fmt, ...);
int uftp_ui_should_draw(uftp_ui_t *ui, uint64_t interval_ms);

void uftp_ui_draw_sender(uftp_ui_t *ui, const uftp_stats_t *stats,
                         const uftp_send_window_t *win, uint32_t total_seqs,
                         uint64_t file_size, uint32_t session_id);

void uftp_ui_draw_receiver(uftp_ui_t *ui, const uftp_stats_t *stats,
                           const uftp_recv_window_t *win, uint32_t total_seqs,
                           uint64_t bytes_written, uint32_t session_id,
                           uint16_t port);

#endif
