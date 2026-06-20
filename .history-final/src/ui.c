#include "uftp/ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef UFTP_HAS_NCURSES
#include <ncurses.h>
#endif

static void ui_begin_frame(uftp_ui_t *ui) {
    if (!ui->enabled) {
        return;
    }
#ifdef UFTP_HAS_NCURSES
    erase();
#else
    if (!ui->active) {
        fputs("\033[?1049h\033[?25l", stdout);
        ui->active = 1;
    }
    fputs("\033[H", stdout);
#endif
}

static void ui_end_frame(uftp_ui_t *ui) {
    if (!ui->enabled || !ui->active) {
        return;
    }
#ifdef UFTP_HAS_NCURSES
    refresh();
#else
    fflush(stdout);
#endif
}

void uftp_ui_init(uftp_ui_t *ui, int enabled, const char *role) {
    memset(ui, 0, sizeof(*ui));
    ui->enabled = enabled;
    strncpy(ui->role, role, sizeof(ui->role) - 1);
    if (!enabled) {
        return;
    }
#ifdef UFTP_HAS_NCURSES
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1);
        init_pair(5, COLOR_MAGENTA, -1);
    }
    ui->active = 1;
#endif
}

void uftp_ui_shutdown(uftp_ui_t *ui) {
    if (!ui->enabled) {
        return;
    }
#ifdef UFTP_HAS_NCURSES
    if (ui->active) {
        endwin();
    }
#else
    if (ui->active) {
        fputs("\033[?25h\033[?1049l", stdout);
        fflush(stdout);
    }
#endif
    ui->active = 0;
}

void uftp_ui_log(uftp_ui_t *ui, const char *fmt, ...) {
    char line[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (!ui->enabled) {
        uftp_log("%s", line);
        return;
    }

    if (ui->log_count < UFTP_UI_LOG_LINES) {
        strncpy(ui->log[ui->log_count], line, sizeof(ui->log[0]) - 1);
        ui->log_count++;
    } else {
        memmove(ui->log[0], ui->log[1],
                (size_t)(UFTP_UI_LOG_LINES - 1) * sizeof(ui->log[0]));
        strncpy(ui->log[UFTP_UI_LOG_LINES - 1], line, sizeof(ui->log[0]) - 1);
    }
}

int uftp_ui_should_draw(uftp_ui_t *ui, uint64_t interval_ms) {
    if (!ui->enabled) {
        return 0;
    }
    uint64_t now = uftp_now_ms();
    if (now - ui->last_draw_ms >= interval_ms) {
        ui->last_draw_ms = now;
        return 1;
    }
    return 0;
}

static void ui_sample_throughput(uftp_ui_t *ui, const uftp_stats_t *stats,
                                 int sender_side) {
    uint64_t now = uftp_now_ms();
    uint64_t bytes = sender_side ? stats->bytes_sent : stats->bytes_recv;
    if (ui->last_sample_ms == 0) {
        ui->last_sample_ms = now;
        ui->last_bytes = bytes;
        return;
    }
    uint64_t dt = now - ui->last_sample_ms;
    if (dt < 80) {
        return;
    }
    double sec = (double)dt / 1000.0;
    double mbps = ((double)(bytes - ui->last_bytes) * 8.0) / (sec * 1000000.0);
    ui->spark[ui->spark_head] = mbps;
    ui->spark_head = (ui->spark_head + 1) % UFTP_UI_SPARK_SAMPLES;
    ui->last_sample_ms = now;
    ui->last_bytes = bytes;
}

static double ui_peak_mbps(const uftp_ui_t *ui) {
    double peak = 0.1;
    for (int i = 0; i < UFTP_UI_SPARK_SAMPLES; i++) {
        if (ui->spark[i] > peak) {
            peak = ui->spark[i];
        }
    }
    return peak;
}

static void ui_putch(int pair, char ch) {
#ifdef UFTP_HAS_NCURSES
    if (pair > 0 && has_colors()) {
        attron(COLOR_PAIR(pair));
    }
    addch(ch);
    if (pair > 0 && has_colors()) {
        attroff(COLOR_PAIR(pair));
    }
#else
    static const char *colors[] = {"", "\033[32m", "\033[33m", "\033[31m",
                                   "\033[36m", "\033[35m"};
    if (pair >= 1 && pair <= 5) {
        fputs(colors[pair], stdout);
    }
    fputc(ch, stdout);
    if (pair >= 1 && pair <= 5) {
        fputs("\033[0m", stdout);
    }
#endif
}

static void ui_puts(int row, const char *text) {
#ifdef UFTP_HAS_NCURSES
    (void)row;
    addstr(text);
    addch('\n');
#else
    (void)row;
    fputs(text, stdout);
    fputc('\n', stdout);
#endif
}

static char sender_slot_char(const uftp_send_window_t *win, uint32_t seq,
                             uint32_t total_seqs) {
    if (seq >= total_seqs) {
        return ' ';
    }
    if (seq < win->send_base) {
        return '+';
    }
    if (seq >= win->send_next) {
        return '.';
    }
    const uftp_send_slot_t *slot = &win->slots[seq % win->window];
    if (slot->valid && slot->seq == seq) {
        return slot->retries > 0 ? '!' : '>';
    }
    return '+';
}

static int sender_slot_color(char ch) {
    switch (ch) {
    case '+': return 1;
    case '>': return 4;
    case '!': return 3;
    case '.': return 0;
    default:  return 0;
    }
}

static char recv_slot_char(const uftp_recv_window_t *win, uint32_t seq,
                           uint32_t total_seqs, int *is_gap) {
    *is_gap = 0;
    if (seq >= total_seqs) {
        return ' ';
    }
    if (seq < win->recv_base) {
        return '+';
    }
    const uftp_recv_slot_t *slot = &win->slots[seq % win->window];
    if (slot->valid && slot->seq == seq) {
        return (seq == win->recv_base) ? '=' : '#';
    }
    for (uint32_t later = seq + 1; later < win->recv_base + win->window &&
                                    later < total_seqs; later++) {
        const uftp_recv_slot_t *ls = &win->slots[later % win->window];
        if (ls->valid && ls->seq == later) {
            *is_gap = 1;
            return '_';
        }
    }
    return '.';
}

static int recv_slot_color(char ch) {
    switch (ch) {
    case '+': return 1;
    case '=': return 4;
    case '#': return 5;
    case '_': return 3;
    case '.': return 0;
    default:  return 0;
    }
}

static void ui_render_sparkline(uftp_ui_t *ui, double current_mbps) {
    double peak = ui_peak_mbps(ui);
    if (current_mbps > peak) {
        peak = current_mbps;
    }
    char bar[UFTP_UI_SPARK_SAMPLES + 1];
    int idx = 0;
    for (int n = 0; n < UFTP_UI_SPARK_SAMPLES; n++) {
        int pos = (ui->spark_head + n) % UFTP_UI_SPARK_SAMPLES;
        double sample = ui->spark[pos];
        int level = (int)((sample / peak) * 8.0);
        if (level < 0) {
            level = 0;
        }
        if (level > 8) {
            level = 8;
        }
        bar[idx++] = (char)('0' + level);
    }
    bar[idx] = '\0';
    char line[128];
    snprintf(line, sizeof(line), "  throughput  [%s]  %.2f Mbps (peak %.2f)",
             bar, current_mbps, peak);
    ui_puts(0, line);
}

void uftp_ui_draw_sender(uftp_ui_t *ui, const uftp_stats_t *stats,
                         const uftp_send_window_t *win, uint32_t total_seqs,
                         uint64_t file_size, uint32_t session_id) {
    if (!ui->enabled) {
        return;
    }
    ui_sample_throughput(ui, stats, 1);
    ui_begin_frame(ui);

    char header[128];
    snprintf(header, sizeof(header),
             "uftp %s | session %08x | %u packets | %llu bytes",
             ui->role, session_id, total_seqs, (unsigned long long)file_size);
    ui_puts(0, header);
    ui_puts(0, "================================================================");

    char legend[96];
    snprintf(legend, sizeof(legend),
             "send window  base=%u  next=%u  in-flight=%d",
             win->send_base, win->send_next, uftp_send_win_in_flight(win));
    ui_puts(0, legend);
    ui_puts(0, "  . pending   > in-flight   + acked   ! retransmit");

#ifdef UFTP_HAS_NCURSES
    move(5, 2);
#else
    fputs("  ", stdout);
#endif
    for (int i = 0; i < (int)win->window; i++) {
        uint32_t seq = win->send_base + (uint32_t)i;
        char ch = sender_slot_char(win, seq, total_seqs);
        ui_putch(sender_slot_color(ch), ch);
    }
    ui_puts(0, "");

    uint64_t elapsed = uftp_now_ms() - stats->start_ms;
    if (elapsed == 0) {
        elapsed = 1;
    }
    double sec = (double)elapsed / 1000.0;
    double avg_mbps = ((double)stats->bytes_sent * 8.0) / (sec * 1000000.0);
    ui_render_sparkline(ui, avg_mbps);

    snprintf(header, sizeof(header),
             "  acked %u/%u   sent %llu B   retx %llu   acks %llu",
             win->send_base, total_seqs,
             (unsigned long long)stats->bytes_sent,
             (unsigned long long)stats->retransmits,
             (unsigned long long)stats->acks_recv);
    ui_puts(0, header);
    ui_puts(0, "");
    ui_puts(0, "event log");

    for (int i = 0; i < UFTP_UI_LOG_LINES; i++) {
        if (ui->log[i][0] != '\0') {
            char logline[112];
            snprintf(logline, sizeof(logline), "  %s", ui->log[i]);
            ui_puts(0, logline);
        }
    }

    ui_puts(0, "================================================================");
    ui_puts(0, "Ctrl+C to abort");
    ui_end_frame(ui);
}

void uftp_ui_draw_receiver(uftp_ui_t *ui, const uftp_stats_t *stats,
                           const uftp_recv_window_t *win, uint32_t total_seqs,
                           uint64_t bytes_written, uint32_t session_id,
                           uint16_t port) {
    if (!ui->enabled) {
        return;
    }
    ui_sample_throughput(ui, stats, 0);
    ui_begin_frame(ui);

    char header[128];
    snprintf(header, sizeof(header),
             "uftp %s | port %u | session %08x | %u packets",
             ui->role, port, session_id, total_seqs);
    ui_puts(0, header);
    ui_puts(0, "================================================================");

    char legend[96];
    snprintf(legend, sizeof(legend),
             "recv buffer  base=%u  gaps=%llu",
             win->recv_base, (unsigned long long)stats->gaps_detected);
    ui_puts(0, legend);
    ui_puts(0, "  . empty   = next expected   # buffered   _ gap   + delivered");

#ifdef UFTP_HAS_NCURSES
    move(5, 2);
#else
    fputs("  ", stdout);
#endif
    for (int i = 0; i < (int)win->window; i++) {
        uint32_t seq = win->recv_base + (uint32_t)i;
        int is_gap = 0;
        char ch = recv_slot_char(win, seq, total_seqs, &is_gap);
        (void)is_gap;
        ui_putch(recv_slot_color(ch), ch);
    }
    ui_puts(0, "");

    uint64_t elapsed = uftp_now_ms() - stats->start_ms;
    if (elapsed == 0) {
        elapsed = 1;
    }
    double sec = (double)elapsed / 1000.0;
    double avg_mbps = ((double)stats->bytes_recv * 8.0) / (sec * 1000000.0);
    ui_render_sparkline(ui, avg_mbps);

    snprintf(header, sizeof(header),
             "  delivered %u/%u   written %llu B   acks sent %llu",
             win->recv_base, total_seqs,
             (unsigned long long)bytes_written,
             (unsigned long long)stats->acks_sent);
    ui_puts(0, header);
    ui_puts(0, "");
    ui_puts(0, "event log");

    for (int i = 0; i < UFTP_UI_LOG_LINES; i++) {
        if (ui->log[i][0] != '\0') {
            char logline[112];
            snprintf(logline, sizeof(logline), "  %s", ui->log[i]);
            ui_puts(0, logline);
        }
    }

    ui_puts(0, "================================================================");
    ui_puts(0, "Ctrl+C to abort");
    ui_end_frame(ui);
}
