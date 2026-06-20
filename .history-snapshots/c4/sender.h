#ifndef UFTP_SENDER_H
#define UFTP_SENDER_H

#include "net.h"
#include "stats.h"

int uftp_sender_run(const char *host, uint16_t port, const char *filepath);

#endif
