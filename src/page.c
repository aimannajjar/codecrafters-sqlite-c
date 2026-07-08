#include "page.h"
#include "database.h"
#include "endian.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/** reads a page header form current positon in stream
 ** it will all also read the cell_offsets array that immediately
 ** will allocate some data on heap, call btree_page_free to dealloc
 */
int btree_page_read(struct db *db, struct btree_page *header, int page_number,
                    FILE *stream) {

    assert(page_number && "page number should be 1-based");

    header->page_start = (page_number - 1) * db->page_size;
    fseek(stream, header->page_start, SEEK_SET);

    if (page_number == 1)
        fseek(stream, 100, SEEK_SET); // skip 100 byte head that's unique
                                      // to first page

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

    header->cell_offsets = NULL;
    if (header->cells_count) {
        header->cell_offsets = malloc(sizeof(uint16_t) * header->cells_count);
        if (header->cell_offsets == nullptr) {
            perror("malloc");
            return -1;
        }
    }

    for (size_t i = 0; i < header->cells_count; i++) {
        if (!fread_be16(&header->cell_offsets[i], stream)) {
            return -1;
        }
    }

    return 0;
}

int btree_tinterior_cell_read(struct btree_page *header, int index,
                              FILE *stream) {

    int cell_offset = header->cell_offsets[index];
    fseek(stream, header->page_start + cell_offset, SEEK_SET);
    uint32_t left_child_page = 0;
    int c = fread_be32(&left_child_page, stream);
    if (c <= 0 || c >= sizeof left_child_page) {
        fputs("failed to decode left child page number\n", stderr);
        return -1;
    }

    return left_child_page;
}

/** reads an individual cell from the page whose header is defined
 ** by `header`. Use `btree_header_read` to obtain `header` object
 ** this header will contain a dynamic array of fields, call
 ** `btree_tleaf_cell_free` to fully free the contents of the cell
 ** when done with it.
 ** Returns 0 on success, -1 on errors
 */
int btree_tleaf_cell_read(struct btree_tleaf_cell *cell,
                          struct btree_page *header, int index, FILE *stream,
                          int debug) {
    if (header->page_type != TABLE_LEAF_PAGE) {
        printf("attempted to read leaf cell from wrong page type 0x%02x\n",
               header->page_type);
        return -1;
    }
    int cell_offset = header->cell_offsets[index];
    int c;
    fseek(stream, header->page_start + cell_offset, SEEK_SET);

    c = fread_varint(&cell->payload_size, stream);
    if (c <= 0 || c >= sizeof cell->payload_size) {
        fputs("failed to decode payload size\n", stderr);
        return -1;
    }

    c = fread_varint(&cell->rowid, stream);
    if (c <= 0 || c >= sizeof(cell->rowid)) {
        fputs("failed to parse rowid rom payload\n", stderr);
        return -1;
    }

    if (!cell->payload_size) {
        cell->record.fields_count = 0;
        return 0;
    }

    struct record *record = &cell->record; // aliasing for readability
    record->record_start = ftell(stream);
    c = fread_varint(&record->header_size, stream);
    if (c <= 0 || c >= sizeof(record->header_size)) {
        fprintf(stderr, "decoding header_size varint failed; read %d bytes\n",
                c);
        return -1;
    }

    int remaining = record->header_size - c;
    uint64_t column_types[100];
    int i = 0;
    while (remaining) {
        int64_t serial = 0;
        c = fread_varint(&serial, stream);
        if (c <= 0 || c >= sizeof(serial)) {
            fprintf(stderr,
                    "decoding serial type varint failed; read %d bytes\n", c);
            return -1;
        }
        column_types[i++] = serial;
        remaining -= c;
    }

    record->fields_count = i;

    if (record->fields_count == 0) {
        printf("going to overflow\n");
        goto overflow;
    }

    struct field *fields = malloc(sizeof(struct field) * record->fields_count);
    if (!fields) {
        perror("malloc");
        return -1;
    }

    for (i = 0; i < record->fields_count; i++) {
        if (column_types[i] >= 12 && !(column_types[i] & 0x1)) {
            // blobs
            uint32_t size = column_types[i] / 2;
            char *t = malloc(size);
            if (!t) {
                perror("malloc");
                if (i > 0) // clean up previously allocated fields
                    record_fields_free(fields, i - 1);
                return -1;
            }

            if (fread(t, 1, size, stream) < size) {
                perror("fread");
                if (i > 0) // clean up previously allocated fields
                    record_fields_free(fields, i - 1);
                return -1;
            }
            fields[i].type = FIELD_TYPE_BLOB;
            fields[i].data = t;
            fields[i].size = size;
        } else if (column_types[i] >= 13 && (column_types[i] & 0x1)) {
            // text
            uint32_t size = (column_types[i] - 13) / 2;
            char *t = malloc(size + 1);
            if (!t) {
                perror("malloc");
                if (i > 0) // clean up previously allocated fields
                    record_fields_free(fields, i - 1);
                return -1;
            }

            if (fread(t, 1, size, stream) < size) {
                perror("fread");
                free(t);
                if (i > 0) // clean up previously allocated fields
                    record_fields_free(fields, i - 1);
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
            // printf("found numerical serial type: %ld\n", column_types[i]);
            switch (column_types[i]) {
            case 0:
                val = 0; // for now using 0 as SQL NULL
                break;
            case 1:
                val = fgetc(stream);
                break;
            case 2:
                uint16_t val16 = 0;
                if (!fread_be16(&val16, stream)) {
                    if (i > 0) // clean up previously allocated fields
                        record_fields_free(fields, i - 1);
                    puts("error parsing numerical field of serial type 2");
                    return -1;
                }
                val = val16;
                if (debug)
                    val = 5;
                break;
            case 3:
                fseek(stream, 3, SEEK_CUR); // todo read be24
                break;
            case 4:
                uint32_t val32;
                if (!fread_be32(&val32, stream)) {
                    if (i > 0) // clean up previously allocated fields
                        record_fields_free(fields, i - 1);
                    puts("error parsing numerical field of serial type 2");
                    return -1;
                }
                val = val32;
                break;
            case 5:
                fseek(stream, 6, SEEK_CUR); // todo read be48
                break;
            case 6:
                uint64_t val64;
                if (!fread_be64(&val64, stream)) {
                    if (i > 0) // clean up previously allocated fields
                        record_fields_free(fields, i - 1);
                    puts("error parsing numerical field of serial type 2");
                    return -1;
                }
                val = val64;
                break;
            case 7:
                // todo big endian floating numbers
                fseek(stream, 8, SEEK_CUR); // todo read be64
                break;
            case 8:
                val = 0;
                break;
            case 9:
                val = 1;
                break;
            default:
                printf("invalid serial type encountered: 0x%lx\n",
                       column_types[i]);
                if (i > 0) // clean up previously allocated fields
                    record_fields_free(fields, i - 1);
                return -1;
            }

            if (debug && i == 0) {
                fprintf(stderr,
                        "i've just set field whose index is 0 to %ld and "
                        "serial type is %ld\n",
                        column_types[i], val);
                val = 4;
            }
            f.number = val;
            fields[i] = f;
        }
    }
    record->fields = fields;

overflow:
    if (fread_be32(&cell->overflow_page_number, stream) == 0) {
        // fputs("could not read overflow page number\n", stderr);
        // return -1;
    }

    return 0;
}

/** cleans up dynamic allocations in btree_header objects
 ** this will not deallocate the passed `struct btree_header`,
 ** only any dynamic objects inside it
 */
int btree_page_free(struct btree_page *header) {
    if (header->cells_count)
        free(header->cell_offsets);
    header->cell_offsets = NULL;
    return 0;
}

/** cleans up dynamic allocations in btree_leaf_cell objects
 ** this will not deallocate the passed `struct btree_leaf_cell`,
 ** only any dynamic objects inside it
 */
int btree_tleaf_cell_free(struct btree_tleaf_cell *cell) {
    record_fields_free(cell->record.fields, cell->record.fields_count);
    free(cell->record.fields);
    cell->record.fields = NULL;
    return 0;
}

/** cleans up fields array and all its contentse **/
void record_fields_free(struct field *fields, size_t len) {
    for (int i = 0; i < len; i++) {
        if (fields[i].type != FIELD_TYPE_NUMBER) {
            free(fields[i].data);
            fields[i].data = NULL;
        }
    }
}
