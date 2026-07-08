#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>
#include <stdio.h>

// Page Types
#define TABLE_LEAF_PAGE 0x0d
#define TABLE_INTERIOR_PAGE 0x05
#define INDEX_LEAF_PAGE 0x0a
#define INDEX_INTERIOR_PAGE 0x02

struct btree_page {
    unsigned char page_type;
    uint16_t first_freeblock;
    uint16_t cells_count;
    uint32_t cell_content_start;
    unsigned char free_bytes_fragments;
    uint32_t right_ptr;
    uint16_t *cell_offsets;
    long page_start; // absolute poistion in file
};

// records are basically rows
struct record {
    long record_start;
    int64_t header_size;
    uint32_t fields_count;
    struct field *fields;
};

// table leaf and index leaf cells are
// almost identical (with except of rowid)
// so we reuse same structure
struct btree_leaf_cell {
    int64_t payload_size;
    int64_t rowid;
    uint32_t overflow_page_number;
    struct record record; // cell payload
};

// index interior cells actually have
// data, unlike table interior cells
// which only have pointers
struct btree_index_interior_cell {
    uint32_t left_child_pn;
    int64_t payload_size;
    uint32_t overflow_page_number;
    struct record record; // this is the payload
};

enum field_type {
    FIELD_TYPE_NUMBER,
    FIELD_TYPE_BLOB,
    FIELD_TYPE_TEXT,
};

// fields or columns (fields because we refer to specific cell in a row)
struct field {
    enum field_type type;
    size_t size;
    union {
        char *data;
        int64_t number;
    };
};

struct db;

void record_fields_free(struct field *fields, size_t len);
int btree_page_read(struct db *db, struct btree_page *header, int first,
                    FILE *stream);
int btree_page_free(struct btree_page *header);
int btree_leaf_cell_free(struct btree_leaf_cell *cell);
int btree_iinterior_cell_free(struct btree_index_interior_cell *cell);
int btree_record_read(int rowid, struct record *record, FILE *stream);
int btree_leaf_cell_read(struct btree_leaf_cell *cell,
                         struct btree_page *header, int index, FILE *stream);
int btree_cell_read(struct btree_leaf_cell *cell, struct btree_page *header,
                    int index, FILE *stream);
int btree_tinterior_cell_read(struct btree_page *header, int index,
                              FILE *stream);
int btree_iinterior_cell_read(struct btree_index_interior_cell *cell,
                              struct btree_page *header, int index,
                              FILE *stream);
#endif
