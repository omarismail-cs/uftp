#include "uftp/sender.h"
#include "uftp/receiver.h"
#include "uftp/opts.h"
#include "uftp/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s [options] send <host> <port> <file>\n"
            "  %s [options] recv <port> <output_file>\n"
            "\n"
            "Options:\n"
            "  --no-ui          Plain text output instead of the live terminal UI\n"
            "  --ui             Live terminal UI (default)\n"
            "  --drop <pct>     Drop pct%% of incoming packets (0-100, for testing)\n"
            "  --window <n>     Send window size (%u-%u, default %u)\n"
            "  -w <n>           Alias for --window\n"
            "  --mss <n>        Chunk payload size (%u-%u, default %u)\n"
            "  -m <n>           Alias for --mss\n",
            prog, prog,
            UFTP_WINDOW_MIN, UFTP_WINDOW_MAX, UFTP_WINDOW_MAX,
            UFTP_MSS_MIN, UFTP_MSS_MAX, UFTP_MSS_MAX);
}

static int parse_common_opts(int argc, char **argv, int *argi, uftp_opts_t *opts) {
    *opts = uftp_opts_default();
    *argi = 1;

    while (*argi < argc && argv[*argi][0] == '-') {
        if (strcmp(argv[*argi], "--no-ui") == 0) {
            opts->use_ui = 0;
            (*argi)++;
        } else if (strcmp(argv[*argi], "--ui") == 0) {
            opts->use_ui = 1;
            (*argi)++;
        } else if (strcmp(argv[*argi], "--drop") == 0) {
            if (*argi + 1 >= argc) {
                return -1;
            }
            opts->drop_pct = atoi(argv[*argi + 1]);
            *argi += 2;
        } else if (strcmp(argv[*argi], "--window") == 0 ||
                   strcmp(argv[*argi], "-w") == 0) {
            if (*argi + 1 >= argc) {
                return -1;
            }
            opts->window = (uint16_t)atoi(argv[*argi + 1]);
            *argi += 2;
        } else if (strcmp(argv[*argi], "--mss") == 0 ||
                   strcmp(argv[*argi], "-m") == 0) {
            if (*argi + 1 >= argc) {
                return -1;
            }
            opts->mss = (uint16_t)atoi(argv[*argi + 1]);
            *argi += 2;
        } else {
            break;
        }
    }
    return uftp_opts_validate(opts);
}

static void print_drop_stats(const uftp_opts_t *opts) {
    uint64_t dropped = 0;
    uint64_t seen = 0;

    if (!opts || opts->drop_pct <= 0) {
        return;
    }

    uftp_net_get_drop_stats(&dropped, &seen);
    if (seen > 0) {
        fprintf(stderr, "  sim drops:    %llu / %llu incoming (%.1f%%)\n",
                (unsigned long long)dropped,
                (unsigned long long)seen,
                (double)dropped * 100.0 / (double)seen);
    }
}

int main(int argc, char **argv) {
    uftp_opts_t opts;
    int argi = 0;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (parse_common_opts(argc, argv, &argi, &opts) != 0) {
        usage(argv[0]);
        return 1;
    }

    if (argi >= argc) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[argi], "send") == 0) {
        if (argc - argi != 4) {
            usage(argv[0]);
            return 1;
        }
        const char *host = argv[argi + 1];
        uint16_t port = (uint16_t)atoi(argv[argi + 2]);
        const char *file = argv[argi + 3];
        int rc = uftp_sender_run(host, port, file, &opts);
        print_drop_stats(&opts);
        return rc;
    }

    if (strcmp(argv[argi], "recv") == 0) {
        if (argc - argi != 3) {
            usage(argv[0]);
            return 1;
        }
        uint16_t port = (uint16_t)atoi(argv[argi + 1]);
        const char *out = argv[argi + 2];
        int rc = uftp_receiver_run(port, out, &opts);
        print_drop_stats(&opts);
        return rc;
    }

    usage(argv[0]);
    return 1;
}
