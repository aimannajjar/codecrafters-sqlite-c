#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>
#include <stdio.h>

// Page Types
#define TABLE_LEAF_PAGE 0x0d
#define TABLE_INTERIOR_PAGE 0x05
#define INDEX_LEAF_PAGE 0x0a
#define INDEX_INTERIOR_PAGE 0x02

struct btree_header {
    unsigned char page_type;
    uint16_t first_freeblock;
    uint16_t cells_count;
    uint16_t cell_content_start;
    unsigned char free_bytes_framgnets;
    uint32_t right_ptr;
    uint16_t *cell_offsets;
    long page_start; // absolute poistion in file
};

struct btree_tleaf_cell {
    int64_t payload_size;
    int64_t rowid;
    unsigned char *payload;
    uint32_t overflow_page_number;
};

struct record {
    long record_start;
    int64_t header_size;
};

int btree_header_read(struct btree_header *header, FILE *stream);
int btree_header_free(struct btree_header *header);
int btree_tleaf_cell_read(struct btree_tleaf_cell *cell,
                          struct btree_header *header, int index, FILE *stream);

#endif
