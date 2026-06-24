#ifndef ENDIAN_H
#define ENDIAN_H

#include <stdint.h>
#include <stdio.h>

static inline int fread_be16(uint16_t *out, FILE *stream) {
    unsigned char buf[2];
    if (fread(buf, 1, 2, stream) < 2)
        return 0;

    *out = (uint16_t)buf[0] << 8 | buf[1];
    return 1;
}

#endif
