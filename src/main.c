#include "uftp/sender.h"
#include "uftp/receiver.h"
#include "uftp/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s send <host> <port> <file>\n"
            "  %s recv <port> <output_file>\n",
            prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "send") == 0) {
        if (argc != 5) {
            usage(argv[0]);
            return 1;
        }
        return uftp_sender_run(argv[2], (uint16_t)atoi(argv[3]), argv[4]);
    }

    if (strcmp(argv[1], "recv") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 1;
        }
        return uftp_receiver_run((uint16_t)atoi(argv[2]), argv[3]);
    }

    usage(argv[0]);
    return 1;
}
