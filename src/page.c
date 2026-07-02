#include "page.h"
#include "database.h"
#include "endian.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int btree_header_read(struct btree_header *header, int first, FILE *stream) {
    header->page_start = ftell(stream);
    if (first)
        header->page_start -= 100;

    if (fread(&header->page_type, 1, 1, stream) != 1) {
        return -1;
    }

    if (!fread_be16(&header->first_freeblock, stream)) {
        return -1;
    }

    if (!fread_be16(&header->cells_count, stream)) {
        return -1;
    }

    uint16_t cells_start;
    if (!fread_be16(&cells_start, stream)) {
        return -1;
    }

    if (cells_start == 0) {
        header->cell_content_start = 65536;
    } else {
        header->cell_content_start = cells_start;
    }

    if (fread(&header->free_bytes_fragments, 1, 1, stream) != 1) {
        return -1;
    }
    // |   |   |   |   |
    header->right_ptr = 0;
    if (header->page_type == TABLE_INTERIOR_PAGE) {
        // only interior pages have right_ptr
        if (!fread_be32(&header->right_ptr, stream)) {
            return -1;
        }
    }

    header->cell_offsets = malloc(sizeof(uint16_t) * header->cells_count);
    if (header->cell_offsets == nullptr) {
        perror("malloc");
        return -1;
    }

    for (size_t i = 0; i < header->cells_count; i++) {
        if (!fread_be16(&header->cell_offsets[i], stream)) {
            return -1;
        }
    }

    return 0;
}

int btree_tleaf_cell_read(struct btree_tleaf_cell *cell,
                          struct btree_header *header, int index,
                          FILE *stream) {
    int cell_offset = header->cell_offsets[index];
    fseek(stream, header->page_start + cell_offset, SEEK_SET);

    if (fread_varint(&cell->payload_size, stream) <= 0) {
        return -1;
    }

    if (fread_varint(&cell->rowid, stream) <= 0) {
        return -1;
    }

    struct record *record = &cell->record; // aliasing for readability
    record->record_start = ftell(stream);
    int c;
    if ((c = fread_varint(&record->header_size, stream)) <= 0)
        return -1;

    int remaining = record->header_size - c;
    uint64_t column_types[100];
    int i = 0;
    while (remaining) {
        int64_t serial = 0;
        if ((c = fread_varint(&serial, stream)) <= 0)
            return -1;
        column_types[i++] = serial;
        remaining -= c;
    }

    record->fields_count = i;

    if (record->fields_count == 0)
        goto overflow;

    struct field *fields = malloc(sizeof(struct field) * record->fields_count);

    for (i = 0; i < record->fields_count; i++) {
        if (column_types[i] >= 12 && !(column_types[i] & 0x1)) {
            // blobs
            uint32_t size = column_types[i] / 2;
            char *t = malloc(size);
            if (fread(t, 1, size, stream) < size) {
                perror("fread");
                return -1;
            }
            fields[i].type = FIELD_TYPE_BLOB;
            fields[i].data = t;
            fields[i].size = size;
        } else if (column_types[i] >= 13 && (column_types[i] & 0x1)) {
            // text
            uint32_t size = (column_types[i] - 13) / 2;
            char *t = malloc(size + 1);
            if (fread(t, 1, size, stream) < size) {
                perror("fread");
                return -1;
            }
            t[size] = '\0';
            fields[i].type = FIELD_TYPE_TEXT;
            fields[i].data = t;
            fields[i].size = size + 1; // add 1 for \0
        } else {
            // numerical types
            // test, probably buggy to types quirks
            struct field f = {
                .type = FIELD_TYPE_NUMBER,
            };

            int64_t val = 0;
            switch (column_types[i]) {
            case 1:
                val = fgetc(stream);
                break;
            case 2:
                uint16_t val16;
                if (!fread_be16(&val16, stream)) {
                    puts("error parsing numerical field of serial type 2");
                    break;
                }
                val = val16;
                break;
            case 3:
                fseek(stream, 3, SEEK_CUR); // todo read be24
                break;
            case 4:
                uint32_t val32;
                if (!fread_be32(&val32, stream)) {
                    puts("error parsing numerical field of serial type 2");
                    break;
                }
                val = val32;
                break;
            case 5:
                fseek(stream, 6, SEEK_CUR); // todo read be48
                break;
            case 6:
                fseek(stream, 8, SEEK_CUR); // todo read be64
                break;
            default:
                puts("invalid serial type encountered");
                break;
            }

            f.number = val;
            fields[i] = f;
        }
    }
    record->fields = fields;

overflow:
    if (fread_be32(&cell->overflow_page_number, stream) <= 0) {
        return -1;
    }

    return 0;
}

int btree_header_free(struct btree_header *header) {
    free(header->cell_offsets);
    return 0;
}

int btree_tleaf_cell_free(struct btree_tleaf_cell *cell) {
    for (int i = 0; i < cell->record.fields_count; i++) {
        if (cell->record.fields[i].type != FIELD_TYPE_NUMBER)
            free(cell->record.fields[i].data);
    }
    free(cell->record.fields);
    return 0;
}
