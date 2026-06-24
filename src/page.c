#include "page.h"
#include "database.h"
#include "endian.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int btree_header_read(struct btree_header *header, FILE *stream) {
    header->page_start = ftell(stream);
    if (fread(&header->page_type, 1, 1, stream) != 1) {
        return -1;
    }

    if (!fread_be16(&header->first_freeblock, stream)) {
        return -1;
    }

    if (!fread_be16(&header->cells_count, stream)) {
        return -1;
    }

    if (!fread_be16(&header->cell_content_start, stream)) {
        return -1;
    }

    // |   |   |   |   |
    header->right_ptr = 0;
    if (header->page_type == TABLE_INTERIOR_PAGE) {
        if (!fread_be32(&header->right_ptr, stream)) {
            return -1;
        }
    }

    header->cell_offsets = malloc(sizeof(uint16_t) * header->cells_count);
    if (header->cell_offsets == nullptr) {
        perror("malloc");
        return -1;
    }

    for (int i = 0; i < header->cells_count; i++) {
        if (!fread_be16(header->cell_offsets + i, stream)) {
            return -1;
        }
    }

    return 0;
}

int btree_tleaf_cell_read(struct btree_tleaf_cell *cell,
                          struct btree_header *header, int index,
                          FILE *stream) {
    int cell_offset = header->cell_offsets[index];
    fseek(stream, header->page_start + header->cell_content_start, SEEK_SET);

    if (fread_varint(&cell->payload_size, stream) <= 0) {
        return -1;
    }

    if (fread_varint(&cell->rowid, stream) <= 0) {
        return -1;
    }

    struct record record;
    record.header_size = ftell(stream);
    int c;
    if ((c = fread_varint(&record.header_size, stream)) <= 0)
        return -1;
    printf("first record's size is %ld\n", record.header_size);
    int remaining = record.header_size - c;
    uint64_t columns[100];
    int i = 0;
    while (remaining) {
        int64_t serial = 0;
        if ((c = fread_varint(&serial, stream)) <= 0)
            return -1;
        columns[i++] = serial;
        printf("columns[%d] = %ld\n", i - 1, serial);
        remaining -= c;
    }
    int record_count = i;

    for (i = 0; i < record_count; i++) {
        if (columns[i] >= 12 && !(columns[i] & 0x1)) {
            uint32_t size = columns[i] / 2;
        } else if (columns[i] >= 13 && (columns[i] & 0x1)) {
            // strings
            uint32_t size = (columns[i] - 13) / 2;
            char *t = malloc(size + 1);
            if (fread(t, 1, size, stream) < size) {
                perror("fread");
                return -1;
            }
            t[size - 1] = '\0';
            printf("READ TEXT: %s\n", t);
        }
    }

    return 0;
}

int btree_header_free(struct btree_header *header) {
    free(header->cell_offsets);
    return 0;
}
