#include "uftp/receiver.h"
#include "uftp/codec.h"
#include "uftp/window.h"
#include "uftp/fileio.h"
#include "uftp/net.h"

#include <stdio.h>

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

int uftp_receiver_run(uint16_t port, const char *outpath) {
    uftp_sock_t sock;
    uftp_recv_window_t win;
    uftp_stats_t stats;
    uftp_file_t file;
    uint8_t rxbuf[UFTP_MAX_PACKET];
    uint8_t chunk[UFTP_MSS_MAX];
    uint32_t session_id = 0;
    uint32_t total_seqs = 0;
    int active = 0;
    int fin_received = 0;
    uint64_t last_ack_ms = 0;

    if (uftp_net_init() != 0) {
        return 1;
    }
    if (uftp_sock_open(&sock, port) != 0) {
        uftp_net_cleanup();
        return 1;
    }

    uftp_stats_init(&stats);
    uftp_recv_win_init(&win, UFTP_WINDOW_MAX);
    memset(&file, 0, sizeof(file));
    uftp_log("listening on UDP port %u", port);

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
                total_seqs = (msg.hdr.total_size + UFTP_MSS_MAX - 1) / UFTP_MSS_MAX;
                if (uftp_file_open_write(&file, outpath, msg.hdr.total_size) != 0) {
                    uftp_sock_close(&sock);
                    uftp_net_cleanup();
                    return 1;
                }
                uftp_recv_win_init(&win, UFTP_WINDOW_MAX);
                active = 1;

                uftp_packet_t hello_ack;
                uftp_hdr_init(&hello_ack.hdr, UFTP_MSG_HELLO_ACK, session_id);
                size_t len = 0;
                uftp_packet_encode(&hello_ack, &len);
                uftp_sock_send(&sock, &hello_ack, len);

                uftp_log("incoming %u bytes (%u pkts) -> %s",
                         msg.hdr.total_size, total_seqs, outpath);
                break;

            case UFTP_MSG_DATA:
                if (!active || msg.hdr.session_id != session_id) {
                    break;
                }
                if (msg.hdr.seq < win.recv_base) {
                    break;
                }
                uftp_recv_win_store(&win, msg.hdr.seq, msg.payload, msg.hdr.payload_len);

                uint16_t chunk_len;
                uint32_t seq;
                while (uftp_recv_win_pop_inorder(&win, chunk, &chunk_len, &seq)) {
                    if (uftp_file_write_chunk(&file, chunk, chunk_len) != 0) {
                        uftp_file_close(&file);
                        uftp_sock_close(&sock);
                        uftp_net_cleanup();
                        return 1;
                    }
                }
                send_ack(&sock, session_id, &win, &stats);
                last_ack_ms = now;
                break;

            case UFTP_MSG_FIN:
                if (!active || msg.hdr.session_id != session_id) {
                    break;
                }
                fin_received = 1;
                uftp_packet_t fin_ack;
                uftp_hdr_init(&fin_ack.hdr, UFTP_MSG_FIN_ACK, session_id);
                size_t flen = 0;
                uftp_packet_encode(&fin_ack, &flen);
                uftp_sock_send(&sock, &fin_ack, flen);
                break;
            default:
                break;
            }
        } else if (active && now - last_ack_ms >= 100) {
            send_ack(&sock, session_id, &win, &stats);
            last_ack_ms = now;
        }
    }

    if (active) {
        uftp_log("complete: %s (%llu bytes)", outpath, (unsigned long long)file.offset);
    }
    uftp_stats_print(&stats);
    uftp_file_close(&file);
    uftp_sock_close(&sock);
    uftp_net_cleanup();
    return 0;
}
