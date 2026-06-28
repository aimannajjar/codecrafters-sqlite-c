#include "database.h"
#include "page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <database path> <command>\n", argv[0]);
        return 1;
    }

    int result = EXIT_SUCCESS;
    const char *database_file_path = argv[1];
    const char *command = argv[2];

    FILE *database_file = fopen(database_file_path, "rb");
    if (!database_file) {
        perror(argv[1]);
        return 1;
    }

    struct db db;
    if (db_header_read(&db, database_file) != 0) {
        puts("failed to parse header");
        result = EXIT_FAILURE;
        goto close;
    }

    struct btree_header first_page;
    if (btree_header_read(&first_page, 1, database_file) != 0) {
        puts("failed to parse page header");
        result = EXIT_FAILURE;
        goto close;
    }

    if (first_page.page_type != TABLE_LEAF_PAGE) {
        puts("invalid first page");
        goto close;
    }

    if (strcmp(command, ".dbinfo") == 0) {
        printf("database page size: %u\n", (unsigned)db.page_size);
        printf("number of tables: %u\n", (unsigned)first_page.cells_count);
    } else if (strcmp(command, ".tables") == 0) {

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
        for (int r = 0; r < first_page.cells_count; r++) {
            struct btree_tleaf_cell cell;
            if (btree_tleaf_cell_read(&cell, &first_page, r, database_file)) {
                puts("failed to parse schema page");
            }

            if (!cell.record->fields_count)
                break;

            // we're interested in col0 and col2
            //  type sould be "table", tbl_name will contain the table name
            if (cell.record->fields[0].type == FIELD_TYPE_TEXT &&
                strcmp(cell.record->fields[0].data, "table") == 0) {
                printf("%s\t", cell.record->fields[2].data);
            }
            btree_tleaf_cell_free(&cell);
        }
        puts("");
    }

free_header:
    btree_header_free(&first_page);

close:
    fclose(database_file);

    return result;
}

