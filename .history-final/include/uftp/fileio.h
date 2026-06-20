#ifndef UFTP_FILEIO_H
#define UFTP_FILEIO_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *fp;
    uint64_t size;
    uint64_t offset;
    char path[512];
} uftp_file_t;

int uftp_file_open_read(uftp_file_t *f, const char *path);
int uftp_file_open_write(uftp_file_t *f, const char *path, uint64_t size);
int uftp_file_read_chunk(uftp_file_t *f, uint8_t *buf, uint16_t cap,
                         uint16_t *out_len);
int uftp_file_write_chunk(uftp_file_t *f, const uint8_t *buf, uint16_t len);
void uftp_file_close(uftp_file_t *f);

#endif
