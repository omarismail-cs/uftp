#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s send <host> <port> <file>\n", prog);
    fprintf(stderr, "       %s recv <port> <output>\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    fprintf(stderr, "[uftp] entry skeleton ready (send/recv not wired yet)\n");
    return 1;
}
