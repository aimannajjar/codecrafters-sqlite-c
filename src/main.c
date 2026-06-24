#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <database path> <command>\n", argv[0]);
        return 1;
    }

    int retval = EXIT_SUCCESS;
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
        retval = EXIT_FAILURE;
        goto close;
    }

    if (strcmp(command, ".dbinfo") == 0) {
        printf("database page size: %u\n", (unsigned)db.page_size);
    }

close:
    fclose(database_file);

    return retval;
}
