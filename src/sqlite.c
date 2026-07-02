#include "database.h"
#include "page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SQL_STMT_LEN 4096

int sqlite_cmd_sql_stmt(const char *stmt, struct db *db, FILE *database_file) {
    // SQL command
    char target[100];
    char sql_stmt[MAX_SQL_STMT_LEN] = {0};
    strncpy(sql_stmt, stmt, MAX_SQL_STMT_LEN);
    sql_stmt[MAX_SQL_STMT_LEN - 1] = '\0';

    char *s = sql_stmt;
    while (sscanf(s, "%s", target) >= 0) {
        s += strlen(target) + 1;
    }

    // read sqlite_schema table;
    struct schema_record *records;
    int rlen;
    if ((rlen = read_schema_table(&records, database_file)) < 0) {
        return -1;
    }

    // find the table we're interested in
    size_t i = 0;
    for (i = 0; i < rlen; i++) {
        if (strcmp(records[i].tbl_name, target) == 0) {
            goto found;
        }
    }
    goto not_found;

found:
    // read root page of target table
    struct btree_header page_header;
    fseek(database_file, (records[i].rootpage - 1) * db->page_size, SEEK_SET);
    btree_header_read(&page_header, 0, database_file);
    printf("%d\n", page_header.cells_count);

    btree_header_free(&page_header);
    free(records);
    return 0;

not_found:
    printf("table '%s' does not exist\n", target);
    free(records);
    return -1;
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
    struct schema_record *records;
    int rlen;
    if ((rlen = read_schema_table(&records, database_file)) < 0) {
        return -1;
    }

    for (int i = 0; i < rlen; i++) {
        printf("%s\t", records[i].tbl_name);
    }

    puts("");

    free(records);
    return 0;
}

