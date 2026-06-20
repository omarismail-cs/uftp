#include "uftp/sender.h"
#include "uftp/codec.h"
#include "uftp/window.h"
#include "uftp/fileio.h"
#include "uftp/ui.h"
#include "uftp/net.h"

#include <stdio.h>

static int send_packet(uftp_sock_t *sock, uftp_packet_t *pkt, uftp_stats_t *stats,
                       int is_retx) {
    size_t len = 0;
    if (uftp_packet_encode(pkt, &len) != 0) {
        return -1;
    }
    if (uftp_sock_send(sock, pkt, len) < 0) {
        return -1;
    }
    stats->packets_sent++;
    stats->bytes_sent += len;
    if (is_retx) {
        stats->retransmits++;
    }
    return 0;
}

static void process_ack(uftp_send_window_t *win, const uftp_packet_t *ack,
                        uftp_stats_t *stats) {
    stats->acks_recv++;

    uint32_t cum = ack->hdr.cum_ack;
    while (win->send_base < cum) {
        uftp_send_win_mark_acked(win, win->send_base);
        win->send_base++;
    }

    uint64_t bitmap = ack->hdr.sack_bitmap;
    uint32_t sack_limit = win->window - 1;
    if (sack_limit > UFTP_SACK_BITS) {
        sack_limit = UFTP_SACK_BITS;
    }
    for (uint32_t i = 0; i < sack_limit; i++) {
        if (bitmap & (1ULL << i)) {
            uftp_send_win_mark_acked(win, cum + 1 + i);
        }
    }
    uftp_send_win_advance(win);
}

static int handshake(uftp_sock_t *sock, uftp_file_t *file, uint32_t session_id,
                     uint32_t file_crc, uint16_t window, uint16_t mss,
                     uftp_stats_t *stats, uftp_ui_t *ui) {
    uftp_packet_t pkt;
    uint8_t rxbuf[UFTP_MAX_PACKET];
    int acked = 0;

    for (int attempt = 0; attempt < 10 && !acked; attempt++) {
        uftp_hdr_init(&pkt.hdr, UFTP_MSG_HELLO, session_id);
        pkt.hdr.total_size = (uint32_t)file->size;
        pkt.hdr.window = window;
        pkt.hdr.seq = mss;
        pkt.hdr.flags = UFTP_FLAG_FILE_CRC;
        pkt.hdr.cum_ack = file_crc;

        size_t nlen = strlen(file->path);
        if (nlen > UFTP_MSS_MAX) {
            nlen = UFTP_MSS_MAX;
        }
        memcpy(pkt.payload, file->path, nlen);
        pkt.hdr.payload_len = (uint16_t)nlen;

        send_packet(sock, &pkt, stats, 0);

        int n = uftp_sock_recv(sock, rxbuf, sizeof(rxbuf), 500, NULL);
        if (n <= 0) {
            continue;
        }
        uftp_packet_t resp;
        if (uftp_packet_decode(rxbuf, (size_t)n, &resp) != 0) {
            continue;
        }
        if (resp.hdr.type == UFTP_MSG_HELLO_ACK &&
            resp.hdr.session_id == session_id) {
            acked = 1;
        }
    }

    if (!acked) {
        uftp_ui_log(ui, "handshake failed");
        return -1;
    }
    uftp_ui_log(ui, "handshake ok, %llu bytes, crc %08x, w=%u mss=%u",
                (unsigned long long)file->size, file_crc, window, mss);
    return 0;
}

static int send_fin(uftp_sock_t *sock, uint32_t session_id, uint32_t file_crc,
                    uftp_stats_t *stats, uftp_ui_t *ui) {
    uftp_packet_t pkt;
    uint8_t rxbuf[UFTP_MAX_PACKET];

    for (int attempt = 0; attempt < 10; attempt++) {
        uftp_hdr_init(&pkt.hdr, UFTP_MSG_FIN, session_id);
        pkt.hdr.flags = UFTP_FLAG_FILE_CRC;
        pkt.hdr.cum_ack = file_crc;
        send_packet(sock, &pkt, stats, 0);

        int n = uftp_sock_recv(sock, rxbuf, sizeof(rxbuf), 500, NULL);
        if (n <= 0) {
            continue;
        }
        uftp_packet_t resp;
        if (uftp_packet_decode(rxbuf, (size_t)n, &resp) != 0) {
            continue;
        }
        if (resp.hdr.type == UFTP_MSG_FIN_ACK &&
            resp.hdr.session_id == session_id) {
            if (resp.hdr.cum_ack == UFTP_FIN_ACK_OK) {
                stats->crc_ok = 1;
                uftp_ui_log(ui, "checksum ok");
                return 0;
            }
            stats->crc_ok = 0;
            uftp_ui_log(ui, "checksum FAILED on receiver");
            return -1;
        }
    }
    return -1;
}

int uftp_sender_run(const char *host, uint16_t port, const char *filepath,
                    const uftp_opts_t *opts) {
    uftp_sock_t sock;
    uftp_file_t file;
    uftp_send_window_t win;
    uftp_stats_t stats;
    uftp_ui_t ui;
    uftp_packet_t pkt;
    uint8_t rxbuf[UFTP_MAX_PACKET];
    uint32_t session_id = (uint32_t)uftp_now_ms();
    uint32_t total_seqs;
    uint32_t file_crc = 0;
    uint16_t window = opts ? opts->window : UFTP_WINDOW_MAX;
    uint16_t mss = opts ? opts->mss : UFTP_MSS_MAX;
    int eof = 0;
    uint64_t rto_ms = UFTP_INITIAL_RTO_MS;
    int use_ui = opts ? opts->use_ui : 1;

    uftp_ui_init(&ui, use_ui, "send");

    if (uftp_net_init() != 0) {
        uftp_ui_shutdown(&ui);
        return 1;
    }

    if (opts && opts->drop_pct > 0) {
        uftp_net_set_drop_pct(opts->drop_pct);
        uftp_ui_log(&ui, "dropping %d%% of incoming packets", opts->drop_pct);
    }

    if (uftp_sock_open(&sock, 0) != 0) {
        uftp_net_cleanup();
        uftp_ui_shutdown(&ui);
        return 1;
    }
    if (uftp_sock_set_peer(&sock, host, port) != 0) {
        uftp_sock_close(&sock);
        uftp_net_cleanup();
        uftp_ui_shutdown(&ui);
        return 1;
    }
    if (uftp_file_open_read(&file, filepath) != 0) {
        uftp_sock_close(&sock);
        uftp_net_cleanup();
        uftp_ui_shutdown(&ui);
        return 1;
    }

    file_crc = uftp_file_crc32(filepath);

    uftp_stats_init(&stats);
    stats.file_crc = file_crc;
    uftp_send_win_init(&win, window);

    total_seqs = (uint32_t)((file.size + mss - 1) / mss);
    if (file.size == 0) {
        total_seqs = 0;
    }

    uftp_ui_log(&ui, "connecting to %s:%u", host, port);

    if (handshake(&sock, &file, session_id, file_crc, window, mss, &stats, &ui) != 0) {
        uftp_file_close(&file);
        uftp_sock_close(&sock);
        uftp_net_cleanup();
        uftp_ui_shutdown(&ui);
        return 1;
    }

    uint64_t last_progress = uftp_now_ms();

    while (win.send_base < total_seqs || uftp_send_win_in_flight(&win) > 0) {
        int sent_this_round = 0;
        while (!eof && uftp_send_win_can_send(&win) && sent_this_round < 8) {
            if (win.send_next >= total_seqs) {
                break;
            }

            uftp_send_slot_t *slot = uftp_send_win_slot(&win, win.send_next);
            if (!slot) {
                break;
            }

            uint16_t chunk_len = 0;
            if (!eof) {
                int rd = uftp_file_read_chunk(&file, slot->data, mss, &chunk_len);
                if (rd < 0) {
                    uftp_file_close(&file);
                    uftp_sock_close(&sock);
                    uftp_net_cleanup();
                    uftp_ui_shutdown(&ui);
                    return 1;
                }
                if (chunk_len == 0) {
                    eof = 1;
                    break;
                }
            }

            slot->seq = win.send_next;
            slot->len = chunk_len;
            slot->valid = 1;
            slot->retries = 0;
            slot->sent_at_ms = uftp_now_ms();

            uftp_hdr_init(&pkt.hdr, UFTP_MSG_DATA, session_id);
            pkt.hdr.seq = slot->seq;
            pkt.hdr.payload_len = slot->len;
            memcpy(pkt.payload, slot->data, slot->len);

            send_packet(&sock, &pkt, &stats, 0);
            win.send_next++;
            sent_this_round++;
        }

        for (int i = 0; i < 32; i++) {
            int n = uftp_sock_recv(&sock, rxbuf, sizeof(rxbuf), 0, NULL);
            if (n > 0) {
                uftp_packet_t ack;
                if (uftp_packet_decode(rxbuf, (size_t)n, &ack) == 0 &&
                    ack.hdr.session_id == session_id &&
                    ack.hdr.type == UFTP_MSG_ACK) {
                    process_ack(&win, &ack, &stats);
                }
            } else if (n == 0) {
                break;
            }
        }

        uint64_t now = uftp_now_ms();
        for (uint32_t seq = win.send_base; seq < win.send_next; seq++) {
            uftp_send_slot_t *slot = uftp_send_win_slot(&win, seq);
            if (!slot || !slot->valid || slot->seq != seq) {
                continue;
            }
            if (now - slot->sent_at_ms >= rto_ms) {
                if (slot->retries >= UFTP_MAX_RETRIES) {
                    uftp_ui_log(&ui, "max retries exceeded for seq %u", seq);
                    uftp_file_close(&file);
                    uftp_sock_close(&sock);
                    uftp_net_cleanup();
                    uftp_ui_shutdown(&ui);
                    return 1;
                }
                slot->retries++;
                slot->sent_at_ms = now;
                rto_ms = rto_ms < UFTP_MAX_RTO_MS ? rto_ms * 2 : UFTP_MAX_RTO_MS;

                uftp_hdr_init(&pkt.hdr, UFTP_MSG_DATA, session_id);
                pkt.hdr.seq = slot->seq;
                pkt.hdr.payload_len = slot->len;
                memcpy(pkt.payload, slot->data, slot->len);
                send_packet(&sock, &pkt, &stats, 1);
                uftp_ui_log(&ui, "retransmit seq %u (retry %d)", seq, slot->retries);
            }
        }

        if (use_ui) {
            if (uftp_ui_should_draw(&ui, 33)) {
                uftp_ui_draw_sender(&ui, &stats, &win, total_seqs, file.size,
                                    session_id);
            }
        } else if (now - last_progress >= 500) {
            fprintf(stderr, "\r  progress: %u/%u seqs acked, %llu bytes, %llu retx",
                    win.send_base, total_seqs,
                    (unsigned long long)stats.bytes_sent,
                    (unsigned long long)stats.retransmits);
            fflush(stderr);
            last_progress = now;
        }
    }

    if (!use_ui) {
        fprintf(stderr, "\n");
    }

    uftp_ui_log(&ui, "transfer complete");
    if (send_fin(&sock, session_id, file_crc, &stats, &ui) != 0) {
        uftp_ui_shutdown(&ui);
        uftp_stats_print(&stats);
        uftp_file_close(&file);
        uftp_sock_close(&sock);
        uftp_net_cleanup();
        return 1;
    }
    uftp_ui_shutdown(&ui);
    uftp_stats_print(&stats);

    uftp_file_close(&file);
    uftp_sock_close(&sock);
    uftp_net_cleanup();
    return 0;
}
