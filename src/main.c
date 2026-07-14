#include "database.h"
#include "sql.h"
#include "sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {

    struct sql_query q = sql_parse_new(argv[1]);

    if (q.parse_error) {
        fprintf(stderr, "%s\n", q.parse_error_string);
    } else {
        printf("Parsed:\n");
        printf("Type:\t\t%s\n", (q.type == SQL_SELECT_STATEMENT) ? "SELECT"
                                : (q.type == SQL_SELECT_COUNT_STATEMENT)
                                    ? "SELECT COUNT"
                                    : "CREATE");
        printf("Fields Count:\t%zu\n", q.fields_count);
        printf("Cond. Count:\t%zu\n", q.conditions_count);
        printf("\n");
        printf("---- FIELDS ----\n");
        for (int i = 0; i < q.fields_count; i++) {
            printf("%d - %.*s \n", i, (int)q.fieldsn[i].field_len,
                   q.fieldsn[i].field_name);
        }
        printf("\n");
        printf("---- CONDITIONS ----\n");
        for (int i = 0; i < q.conditions_count; i++) {
            printf("%d - %.*s = ", i, (int)q.conditions[i].field_name_len,
                   q.conditions[i].field_name);
            printf("%.*s\n", (int)q.conditions[i].field_value_len,
                   q.conditions[i].field_value);
        }

    }

    exit(0);

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

    // Dump entire content of database_file in hex
    int ch;
    int i = 1;
    // int second_half = 1257473 / 2;
    // // fprintf(stderr, "DB STARTING FROM %d\n", startp);
    // // fseek(database_file, startp, SEEK_SET);
    // fprintf(stderr, "---\n");
    // while ((ch = fgetc(database_file)) != EOF) {
    //     fprintf(stderr, "%02x", ch);
    //     i++;
    //     if (i == second_half) break;
    // }
    // fprintf(stderr, "---\n");
    // rewind(database_file);
    // printf("total fie size is %d\n", i);

    // Bash command to convert hexdump back to binary:
    // ./a.out | xxd -r -p > output.bin
    struct db db;
    if (db_header_read(&db, database_file) != 0) {
        puts("failed to parse header");
        result = -1;
        goto close;
    }

    if (strcmp(argv[2], ".dbinfo") == 0) {
        if (sqlite_cmd_dbinfo(&db, database_file)) {
            result = EXIT_FAILURE;
            goto close;
        }
    } else if (strcmp(argv[2], ".tables") == 0) {
        if (sqlite_cmd_tables(&db, database_file)) {
            result = EXIT_FAILURE;
            goto close;
        }
    } else {
        if (sqlite_cmd_sql_stmt(argv[2], &db, database_file)) {
            result = EXIT_FAILURE;
            goto close;
        }
    }

close:
    fclose(database_file);

    return result;
}
