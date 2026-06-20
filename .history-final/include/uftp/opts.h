#ifndef UFTP_OPTS_H
#define UFTP_OPTS_H

#include "common.h"
#include <stdint.h>

typedef struct {
    int use_ui;
    int drop_pct;
    uint16_t window;
    uint16_t mss;
} uftp_opts_t;

static inline uftp_opts_t uftp_opts_default(void) {
    uftp_opts_t opts;
    opts.use_ui = 1;
    opts.drop_pct = 0;
    opts.window = UFTP_WINDOW_MAX;
    opts.mss = UFTP_MSS_MAX;
    return opts;
}

int uftp_opts_validate(const uftp_opts_t *opts);

#endif
