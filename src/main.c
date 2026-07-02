#include "database.h"
#include "sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SQL_STMT_LEN 4096

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
        result = -1;
        goto close;
    }

    if (strcmp(argv[2], ".dbinfo") == 0) {
        if (!sqlite_cmd_dbinfo(&db, database_file)) {
            result = EXIT_FAILURE;
            goto close;
        }
    } else if (strcmp(argv[2], ".tables") == 0) {
        if (!sqlite_cmd_tables(&db, database_file)) {
            result = EXIT_FAILURE;
            goto close;
        }
    } else {
        // SQL command
        char word[100];
        char sql_stmt[MAX_SQL_STMT_LEN] = {0};
        strncpy(sql_stmt, argv[2], MAX_SQL_STMT_LEN);
        sql_stmt[MAX_SQL_STMT_LEN-1] = '\0';
        
        char *s = sql_stmt;
        size_t i = 0;
        while (sscanf(s, "%s", word) >= 0) {
            s += strlen(word) + 1;
        }
        printf("Last word: %s\n", word);
    }

close:
    fclose(database_file);

    return result;
}

