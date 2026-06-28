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

    struct btree_header header;
    if (btree_header_read(&header, database_file) != 0) {
        puts("failed to parse page header");
        result = EXIT_FAILURE;
        goto close;
    }

    if (strcmp(command, ".dbinfo") == 0) {
        printf("database page size: %u\n", (unsigned)db.page_size);
        printf("number of tables: %u\n", (unsigned)header.cells_count);
    }
    else if (strcmp(command, ".tables") == 0) {
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, &header, 0, database_file) != 0) {
            puts("failed to parse schema page");
        }
    }

free_header:
    btree_header_free(&header);

close:
    fclose(database_file);

    return result;
}
