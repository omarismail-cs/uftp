#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uftp/net.h"

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s send <host> <port> <file>\n", prog);
    fprintf(stderr, "       %s recv <port> <output>\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (uftp_net_init() != 0) {
        fprintf(stderr, "[uftp] network init failed\n");
        return 1;
    }

    fprintf(stderr, "[uftp] UDP stack ready (send/recv not wired yet)\n");
    uftp_net_cleanup();
    return 1;
}
