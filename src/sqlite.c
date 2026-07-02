#include "database.h"
#include "page.h"
#include <stdio.h>
#include <string.h>

/** Helper function to read the first page header, i.e. schema page's header
 ** it will read the page header and populate cell_offsets
 ** array, use btree_tleaf_cell_read to read individual cells
 ** this assumes database_file is positioned at header position.
 ** returns 0 on success, -1 otherwise
 **/
static int read_schema_page_header(struct btree_header *page_header,
                                   FILE *database_file) {
    if (btree_header_read(page_header, 1, database_file) != 0) {
        puts("failed to parse page header");
        btree_header_free(page_header);
        return -1;
    }

    if (page_header->page_type != TABLE_LEAF_PAGE) {
        puts("invalid first page");
        btree_header_free(page_header);
        return -1;
    }

    return 0;
}

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file) {
    struct btree_header schema_page_header;

    if (read_schema_page_header(&schema_page_header, database_file)) {
        return -1;
    }

    printf("database page size: %u\n", (unsigned)db->page_size);
    printf("number of tables: %u\n", (unsigned)schema_page_header.cells_count);

    return 0;
}

int sqlite_cmd_tables(struct db *db, FILE *database_file) {
    struct btree_header schema_page_header;

    if (read_schema_page_header(&schema_page_header, database_file)) {
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
    size_t cells_len = schema_page_header.cells_count;
    for (int r = 0; r < cells_len; r++) {
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, &schema_page_header, r,
                                  database_file)) {
            puts("failed to parse schema page");
            break;
        }

        if (!cell.record.fields_count) {
            btree_tleaf_cell_free(&cell);
            break;
        }

        // we're interested in col0 and col2
        //  type sould be "table", tbl_name will contain the table name
        if (cell.record.fields[0].type == FIELD_TYPE_TEXT &&
            strcmp(cell.record.fields[0].data, "table") == 0) {
            printf("%s\t", cell.record.fields[2].data);
        }
        btree_tleaf_cell_free(&cell);
    }
    puts("");
    return 0;
}

