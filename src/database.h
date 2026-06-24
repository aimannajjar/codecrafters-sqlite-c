#ifndef DATABASE_H
#define DATABASE_H
#include <stdint.h>
#include <stdio.h>

#define DB_HEADER_SIZE 100
#define DB_HEADER_STRING_LEN 16

struct db {
    unsigned char header_string[DB_HEADER_STRING_LEN];
    uint16_t page_size;
    uint8_t write_version;
    uint8_t read_version;
    uint8_t _reserved;
    uint8_t max_fraction;
    uint8_t min_fraction;
    uint32_t change_counter;
};

int db_header_read(struct db *buf, FILE *stream);

#endif
