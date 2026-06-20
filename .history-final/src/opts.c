#include "uftp/opts.h"
#include "uftp/common.h"

#include <stdio.h>

int uftp_opts_validate(const uftp_opts_t *opts) {
    if (!opts) {
        return 0;
    }
    if (opts->window < UFTP_WINDOW_MIN || opts->window > UFTP_WINDOW_MAX) {
        fprintf(stderr, "window must be %u..%u\n",
                UFTP_WINDOW_MIN, UFTP_WINDOW_MAX);
        return -1;
    }
    if (opts->mss < UFTP_MSS_MIN || opts->mss > UFTP_MSS_MAX) {
        fprintf(stderr, "mss must be %u..%u\n", UFTP_MSS_MIN, UFTP_MSS_MAX);
        return -1;
    }
    if (opts->drop_pct < 0 || opts->drop_pct > 100) {
        fprintf(stderr, "drop must be 0..100\n");
        return -1;
    }
    return 0;
}
