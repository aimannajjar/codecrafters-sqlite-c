#ifndef ENDIAN_H
#define ENDIAN_H

#include <stdint.h>
#include <stdio.h>

/** read unsigned int in big-endian encoding 
 *  returns 0 on error, and 1 on success
 */
static inline int fread_be16(uint16_t *out, FILE *stream) {
    unsigned char buf[2];
    if (fread(buf, 1, 2, stream) < 2)
        return 0;

    *out = (uint16_t)buf[0] << 8 | buf[1];
    return 1;
}

/** read big-endian variable-length int
 *  encoded using Huffman-style encoding
 *  returns number of integer size on success
 *  -1 otherwise
 */
static inline int fread_varint(int64_t *out, FILE *stream) {
    // todo need to handle sign-extension for negative numbers
    int c = 0;
    *out = 0;
    uint64_t acc = 0;
    for (int i = 0; i < 9; i++) {
        unsigned char b;
        if (fread(&b, 1, 1, stream) != 1) {
            return -1;
        }
        unsigned char mask = i < 8 ? 0x7f : 0xff;
        acc <<= (i < 8) ? 7 : 8;
        acc |= (b & mask);
        c++;

        if (!(0x80 & b)) {
            break;
        }
    }
    *out = (int64_t)acc;
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
