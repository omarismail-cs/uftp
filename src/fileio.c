#include "uftp/fileio.h"
#include "uftp/common.h"

int uftp_file_open_read(uftp_file_t *f, const char *path) {
    memset(f, 0, sizeof(*f));
    strncpy(f->path, path, sizeof(f->path) - 1);
    f->fp = fopen(path, "rb");
    if (!f->fp) {
        uftp_log("cannot open for read: %s", path);
        return -1;
    }
    if (fseek(f->fp, 0, SEEK_END) != 0) {
        uftp_file_close(f);
        return -1;
    }
    long sz = ftell(f->fp);
    if (sz < 0) {
        uftp_file_close(f);
        return -1;
    }
    rewind(f->fp);
    f->size = (uint64_t)sz;
    return 0;
}

int uftp_file_open_write(uftp_file_t *f, const char *path, uint64_t size) {
    memset(f, 0, sizeof(*f));
    strncpy(f->path, path, sizeof(f->path) - 1);
    f->size = size;
    f->fp = fopen(path, "wb");
    if (!f->fp) {
        uftp_log("cannot open for write: %s", path);
        return -1;
    }
    return 0;
}

int uftp_file_read_chunk(uftp_file_t *f, uint8_t *buf, uint16_t cap,
                         uint16_t *out_len) {
    size_t n = fread(buf, 1, cap, f->fp);
    if (n == 0 && ferror(f->fp)) {
        return -1;
    }
    f->offset += (uint64_t)n;
    *out_len = (uint16_t)n;
    return (int)n;
}

int uftp_file_write_chunk(uftp_file_t *f, const uint8_t *buf, uint16_t len) {
    size_t n = fwrite(buf, 1, len, f->fp);
    if (n != len) {
        return -1;
    }
    f->offset += (uint64_t)len;
    return 0;
}

void uftp_file_close(uftp_file_t *f) {
    if (f->fp) {
        fclose(f->fp);
        f->fp = NULL;
    }
}
