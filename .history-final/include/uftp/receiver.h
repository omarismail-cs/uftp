#ifndef UFTP_RECEIVER_H
#define UFTP_RECEIVER_H

#include "net.h"
#include "opts.h"
#include "stats.h"

int uftp_receiver_run(uint16_t port, const char *outpath,
                      const uftp_opts_t *opts);

#endif
