#ifndef DATABASE_H
#define DATABASE_H
#include "page.h"
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

struct schema_record {
    char type[100];
    char name[100];
    char tbl_name[100];
    uint64_t rootpge;
    char sql[1024];
};

int db_header_read(struct db *buf, FILE *stream);
int read_schema_page_header(struct btree_header *page_header,
                            FILE *database_file);
int read_schema_table(struct schema_record **records, FILE *database_file);

extern uint16_t db_page_size;
extern enum CHARSET_ENC db_text_encoding;

#endif
