#include "database.h"
#include "endian.h"
#include "page.h"
#include "sql.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum CHARSET_ENC {
    UTF8 = 1,
    UTF_16LE,
    UTF_16BE,
};

enum CHARSET_ENC db_text_encoding = -1;

/** reads the initial bytes of sqlite files, this is typically part
 ** of the first page, but instead we read it separately here, and
 ** then we offset the first page DB_HEADER_SIZE
 ** these include the magic string, the page_size constant and
 ** text encoding among other fields
 **/
int db_header_read(struct db *db, FILE *stream) {

    if (fread(db->header_string, 1, DB_HEADER_STRING_LEN, stream) !=
        DB_HEADER_STRING_LEN) {
        return -1;
    }

    if (!fread_be16(&db->page_size, stream)) {
        return -1;
    }

    // parse encoding at byte 56 then go back
    int r = ftell(stream);
    fseek(stream, 56, SEEK_SET);
    if (!fread_be32(&db_text_encoding, stream)) {
        puts("couldn't parse charset encoding");
        return -1;
    }
    fseek(stream, r, SEEK_SET);

    // skip remaining header fields for now until we really need them
    if (fseek(stream, DB_HEADER_SIZE - DB_HEADER_STRING_LEN - sizeof(uint16_t),
              SEEK_CUR) != 0) {
        perror("fseek");
        return -1;
    }

    return 0;
}

/** Helper function to read the first page header, i.e. schema page's header
 ** it will read the page header and populate cell_offsets
 ** array, use btree_tleaf_cell_read to read individual cells
 ** this assumes database_file is positioned at header position.
 ** returns 0 on success, -1 otherwise
 **/
int db_read_schema_page(struct db *db, struct btree_page *page_header,
                        FILE *database_file) {
    if (btree_page_read(db, page_header, 1, database_file) != 0) {
        puts("failed to parse page header");
        btree_page_free(page_header);
        return -1;
    }

    if (page_header->page_type != TABLE_LEAF_PAGE) {
        puts("invalid first page");
        btree_page_free(page_header);
        return -1;
    }

    return 0;
}

/** reads the sqlite_schema page and allocates *records array pointed to by
 ** the first parameter, returns the number of records or -1 on errors
 */
int db_read_schema_table(struct db *db, struct schema_record **records,
                         FILE *database_file) {
    struct btree_page schema_page_header;

    if (db_read_schema_page(db, &schema_page_header, database_file)) {
        return -1;
    }

    // let's parse sqlite_schema rows (located from first page which we
    // already have) :
    //
    //  CREATE TABLE sqlite_schema (
    //    type text,
    //    name text,
    //    tbl_name text,
    //    rootpage integer,
    //    sql text
    //  );

    // each cell is a row
    struct schema_record *srecs =
        malloc(schema_page_header.cells_count * sizeof **records);

    if (!srecs) {
        perror("malloc");
        btree_page_free(&schema_page_header);
        return -1;
    }

    size_t cells_len = schema_page_header.cells_count;
    int row = 0;
    for (row = 0; row < cells_len; row++) {
        struct btree_leaf_cell cell;
        if (btree_leaf_cell_read(&cell, &schema_page_header, row,
                                  database_file)) {
            puts("failed to parse schema page");
            break;
        }

        // sqlite_schema has 5 columns
        if (cell.record.fields_count != 5) {
            printf("unexpected schema table row size: %d\n",
                   cell.record.fields_count);
            goto error;
        }

        for (int i = 0; i < cell.record.fields_count; i++) {
            enum field_type field_type = cell.record.fields[i].type;
            switch (i) {
            // 'type' column
            case 0:
                if (field_type != FIELD_TYPE_TEXT) {
                    printf("invalid schema field type row=%d,col=%d\n", row, i);
                    goto error;
                }
                strncpy(srecs[row].type, cell.record.fields[i].data,
                        sizeof(srecs[row].type));
                srecs[row].type[sizeof srecs[row].type - 1] = '\0';
                break;
            // 'name' column
            case 1:
                if (field_type != FIELD_TYPE_TEXT) {
                    printf("invalid schema field type row=%d,col=%d\n", row, i);
                    goto error;
                }
                strncpy(srecs[row].name, cell.record.fields[i].data,
                        sizeof(srecs[row].name));
                srecs[row].name[sizeof srecs[row].name - 1] = '\0';
                break;
            // 'tbl_name' column
            case 2:
                if (field_type != FIELD_TYPE_TEXT) {
                    printf("invalid schema field type row=%d,col=%d\n", row, i);
                    goto error;
                }
                strncpy(srecs[row].tbl_name, cell.record.fields[i].data,
                        sizeof(srecs[row].tbl_name));
                srecs[row].tbl_name[sizeof srecs[row].tbl_name - 1] = '\0';
                break;
            // 'rootpage' column
            case 3:
                if (field_type != FIELD_TYPE_NUMBER) {
                    printf("invalid schema field type row=%d,col=%d\n", row, i);
                    goto error;
                }
                srecs[row].rootpage = cell.record.fields[i].number;
                break;
            case 4:
                if (field_type != FIELD_TYPE_TEXT) {
                    printf("invalid schema field type row=%d,col=%d\n", row, i);
                    goto error;
                }
                strncpy(srecs[row].sql, cell.record.fields[i].data,
                        sizeof(srecs[row].sql));
                srecs[row].sql[sizeof srecs[row].sql - 1] = '\0';
                break;
            }
        }
        btree_leaf_cell_free(&cell);

        // populate field_name -> index map
        char *schema_sql = strdup(srecs[row].sql);
        if (!schema_sql) {
            perror("strdup");
            goto error;
        }

        int i = 0;
        char *tokptr;
        struct sql_query q;
        if (sql_parse(schema_sql, &q)) {
            free(schema_sql);
            fputs("failed parse ddl\n", stderr);
            goto error;
        }

        char *schema_field = strtok_r(q.fields_list, ",", &tokptr);
        if (schema_field) {
            do {
                hput(&srecs[row].col_index, strlen(schema_field), schema_field, i++);
            } while ((schema_field = strtok_r(NULL, ",", &tokptr)));
        }
        free(schema_sql);
    }
    btree_page_free(&schema_page_header);
    *records = srecs;

    return cells_len;

error:
    btree_page_free(&schema_page_header);
    free(srecs);
    return -1;
}
