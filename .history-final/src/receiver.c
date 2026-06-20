#include "uftp/receiver.h"
#include "uftp/codec.h"
#include "uftp/window.h"
#include "uftp/fileio.h"
#include "uftp/ui.h"
#include "uftp/net.h"

#include <stdio.h>
#include <stdint.h>

static int send_ack(uftp_sock_t *sock, uint32_t session_id,
                    const uftp_recv_window_t *win, uftp_stats_t *stats) {
    uftp_packet_t pkt;
    uftp_hdr_init(&pkt.hdr, UFTP_MSG_ACK, session_id);
    pkt.hdr.cum_ack = win->recv_base;
    pkt.hdr.sack_bitmap = uftp_recv_win_sack_bitmap(win);
    pkt.hdr.payload_len = 0;

    size_t len = 0;
    if (uftp_packet_encode(&pkt, &len) != 0) {
        return -1;
    }
    if (uftp_sock_send(sock, &pkt, len) < 0) {
        return -1;
    }
    stats->acks_sent++;
    stats->packets_sent++;
    stats->bytes_sent += len;
    return 0;
}

int uftp_receiver_run(uint16_t port, const char *outpath,
                      const uftp_opts_t *opts) {
    uftp_sock_t sock;
    uftp_recv_window_t win;
    uftp_stats_t stats;
    uftp_ui_t ui;
    uftp_file_t file;
    uint8_t rxbuf[UFTP_MAX_PACKET];
    uint8_t chunk[UFTP_MSS_MAX];
    uint32_t session_id = 0;
    uint32_t total_seqs = 0;
    uint16_t session_window = UFTP_WINDOW_MAX;
    uint16_t session_mss = UFTP_MSS_MAX;
    uint32_t expected_crc = 0;
    uint32_t rolling_crc = UFTP_CRC32_INIT;
    int crc_enabled = 0;
    int active = 0;
    int fin_received = 0;
    uint64_t last_ack_ms = 0;
    uint64_t last_progress = 0;
    uint32_t last_gap_log = UINT32_MAX;
    int use_ui = opts ? opts->use_ui : 1;

    uftp_ui_init(&ui, use_ui, "recv");

    if (uftp_net_init() != 0) {
        uftp_ui_shutdown(&ui);
        return 1;
    }

    if (opts && opts->drop_pct > 0) {
        uftp_net_set_drop_pct(opts->drop_pct);
        uftp_ui_log(&ui, "dropping %d%% of incoming packets", opts->drop_pct);
    }
    if (uftp_sock_open(&sock, port) != 0) {
        uftp_net_cleanup();
        uftp_ui_shutdown(&ui);
        return 1;
    }

    uftp_stats_init(&stats);
    uftp_recv_win_init(&win, UFTP_WINDOW_MAX);
    memset(&file, 0, sizeof(file));

    uftp_ui_log(&ui, "listening on UDP port %u", port);

    while (!fin_received) {
        int n = uftp_sock_recv(&sock, rxbuf, sizeof(rxbuf), 50, NULL);
        uint64_t now = uftp_now_ms();

        if (n > 0) {
            uftp_packet_t msg;
            if (uftp_packet_decode(rxbuf, (size_t)n, &msg) != 0) {
                continue;
            }
            stats.packets_recv++;
            stats.bytes_recv += (uint64_t)n;

            switch (msg.hdr.type) {
            case UFTP_MSG_HELLO:
                session_id = msg.hdr.session_id;
                session_window = msg.hdr.window;
                session_mss = (uint16_t)msg.hdr.seq;
                if (session_window < UFTP_WINDOW_MIN ||
                    session_window > UFTP_WINDOW_MAX) {
                    session_window = UFTP_WINDOW_MAX;
                }
                if (session_mss < UFTP_MSS_MIN || session_mss > UFTP_MSS_MAX) {
                    session_mss = UFTP_MSS_MAX;
                }

                if (opts &&
                    (opts->window != session_window || opts->mss != session_mss)) {
                    uftp_ui_log(&ui,
                                "warning: CLI window/mss (%u/%u) differ from "
                                "sender (%u/%u), using sender values",
                                opts->window, opts->mss,
                                session_window, session_mss);
                }

                total_seqs = (msg.hdr.total_size + session_mss - 1) / session_mss;
                if (msg.hdr.total_size == 0) {
                    total_seqs = 0;
                }

                expected_crc = 0;
                rolling_crc = UFTP_CRC32_INIT;
                crc_enabled = 0;
                if (msg.hdr.flags & UFTP_FLAG_FILE_CRC) {
                    expected_crc = msg.hdr.cum_ack;
                    crc_enabled = 1;
                    stats.file_crc = expected_crc;
                }

                if (uftp_file_open_write(&file, outpath, msg.hdr.total_size) != 0) {
                    uftp_sock_close(&sock);
                    uftp_net_cleanup();
                    uftp_ui_shutdown(&ui);
                    return 1;
                }
                uftp_recv_win_init(&win, session_window);
                active = 1;

                uftp_packet_t hello_ack;
                uftp_hdr_init(&hello_ack.hdr, UFTP_MSG_HELLO_ACK, session_id);
                size_t len = 0;
                uftp_packet_encode(&hello_ack, &len);
                uftp_sock_send(&sock, &hello_ack, len);

                uftp_ui_log(&ui, "incoming %u bytes (%u pkts, w=%u mss=%u) -> %s",
                            msg.hdr.total_size, total_seqs,
                            session_window, session_mss, outpath);
                break;

            case UFTP_MSG_DATA:
                if (!active || msg.hdr.session_id != session_id) {
                    break;
                }

                if (msg.hdr.payload_len > session_mss) {
                    break;
                }

                if (msg.hdr.seq < win.recv_base) {
                    break;
                }

                if (msg.hdr.seq >= win.recv_base + win.window) {
                    stats.gaps_detected++;
                    break;
                }

                if (uftp_recv_win_store(&win, msg.hdr.seq, msg.payload,
                                        msg.hdr.payload_len) > 0) {
                    if (msg.hdr.seq > win.recv_base &&
                        !uftp_recv_win_has(&win, win.recv_base)) {
                        stats.gaps_detected++;
                        if (win.recv_base != last_gap_log) {
                            last_gap_log = win.recv_base;
                            uftp_ui_log(&ui, "gap at seq %u", win.recv_base);
                        }
                    }
                }

                uint16_t chunk_len;
                uint32_t seq;
                while (uftp_recv_win_pop_inorder(&win, chunk, &chunk_len, &seq)) {
                    if (crc_enabled) {
                        rolling_crc = uftp_crc32_update(rolling_crc, chunk, chunk_len);
                    }
                    if (uftp_file_write_chunk(&file, chunk, chunk_len) != 0) {
                        uftp_ui_log(&ui, "write failed");
                        uftp_file_close(&file);
                        uftp_sock_close(&sock);
                        uftp_net_cleanup();
                        uftp_ui_shutdown(&ui);
                        return 1;
                    }
                }

                send_ack(&sock, session_id, &win, &stats);
                last_ack_ms = now;

                if (use_ui) {
                    if (uftp_ui_should_draw(&ui, 33)) {
                        uftp_ui_draw_receiver(&ui, &stats, &win, total_seqs,
                                              file.offset, session_id, port);
                    }
                } else if ((now - last_progress) >= 250) {
                    fprintf(stderr, "\r  received: %u/%u seqs, %llu bytes, %llu gaps",
                            win.recv_base, total_seqs,
                            (unsigned long long)file.offset,
                            (unsigned long long)stats.gaps_detected);
                    fflush(stderr);
                    last_progress = now;
                }
                break;

            case UFTP_MSG_FIN:
                if (!active || msg.hdr.session_id != session_id) {
                    break;
                }
                fin_received = 1;

                {
                    int crc_ok = 1;
                    if (crc_enabled) {
                        uint32_t got_crc = uftp_crc32_final(rolling_crc);
                        crc_ok = (got_crc == expected_crc);
                        stats.crc_ok = crc_ok ? 1 : 0;
                        if (crc_ok) {
                            uftp_ui_log(&ui, "checksum verified");
                        } else {
                            uftp_ui_log(&ui, "checksum FAILED (got %08x, expected %08x)",
                                        got_crc, expected_crc);
                        }
                    }

                    uftp_packet_t fin_ack;
                    uftp_hdr_init(&fin_ack.hdr, UFTP_MSG_FIN_ACK, session_id);
                    fin_ack.hdr.cum_ack = crc_ok ? UFTP_FIN_ACK_OK : UFTP_FIN_ACK_FAIL;
                    size_t flen = 0;
                    uftp_packet_encode(&fin_ack, &flen);
                    uftp_sock_send(&sock, &fin_ack, flen);
                }
                break;

            default:
                break;
            }
        } else if (active) {
            if (use_ui && uftp_ui_should_draw(&ui, 33)) {
                uftp_ui_draw_receiver(&ui, &stats, &win, total_seqs, file.offset,
                                      session_id, port);
            }
            if (now - last_ack_ms >= 100) {
                send_ack(&sock, session_id, &win, &stats);
                last_ack_ms = now;
            }
        }
    }

    if (!use_ui) {
        fprintf(stderr, "\n");
    }

    if (active) {
        if (stats.crc_ok == 0) {
            uftp_ui_log(&ui, "transfer failed checksum verification");
        } else if (file.offset != file.size) {
            uftp_ui_log(&ui, "warning: expected %llu, got %llu",
                        (unsigned long long)file.size,
                        (unsigned long long)file.offset);
        } else {
            uftp_ui_log(&ui, "complete: %s (%llu bytes)",
                        outpath, (unsigned long long)file.offset);
        }
    }

    uftp_ui_shutdown(&ui);
    uftp_stats_print(&stats);
    uftp_file_close(&file);
    uftp_sock_close(&sock);
    uftp_net_cleanup();

    if (active && stats.crc_ok == 0) {
        return 1;
    }
    if (active && file.offset != file.size) {
        return 1;
    }
    return 0;
}
