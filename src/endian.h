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

static inline int fread_varint(int64_t *out, FILE *stream) {
    // todo need to handle sign-extension for negative numbers
    int c = 0;
    *out = 0;
    for (int i = 0; i < 9; i++) {
        unsigned char b;
        if (fread(&b, 1, 1, stream) != 1) {
            return -1;
        }
        unsigned char mask = i < 8 ? 0x7f : 0xff;
        *out <<= 7;
        *out |= (b & mask);
        c++;

        if (!(1 << 8 & b)) {
            break;
        }
    }
    return c;
}

static inline int fread_be32(uint32_t *out, FILE *stream) {
    unsigned char buf[4];
    if (fread(buf, 1, 4, stream) != 4) {
        return 0;
    }
    *out = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
    return 1;
}

#endif
