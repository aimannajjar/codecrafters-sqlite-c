#include "sqlite.h"
#include "database.h"
#include "page.h"
#include "sql.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SQL_STMT_LEN 4096

int sqlite_cmd_sql_stmt(char *stmt, struct db *db, FILE *database_file) {
    // SQL command
    char sql_stmt[MAX_SQL_STMT_LEN] = {0};
    strncpy(sql_stmt, stmt, MAX_SQL_STMT_LEN);
    sql_stmt[MAX_SQL_STMT_LEN - 1] = '\0';

    struct sql_query query;
    if (sql_parse(stmt, &query)) {
        return -1;
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
        if (strcmp(records[i].tbl_name, query.table) == 0) {
            goto found;
        }
    }
    goto not_found;

found:
    sqlite_sql_stmt_exec(&records[i], &query, db, database_file);
    free(records);
    return 0;

not_found:
    printf("table '%s' does not exist\n", query.table);
    free(records);
    return -1;
}

static void sqlite_sql_stmt_exec_count(struct btree_header *page_header) {
    printf("%d\n", page_header->cells_count);
}

static int sqlite_sql_stmt_exec_select(struct schema_record *schema,
                                       struct btree_header *page_header,
                                       struct sql_query *query,
                                       FILE *database_file) {
    size_t row_count = page_header->cells_count;
    int row = 0;

    // parse the create sql statement from schema to figure out field index
    // currently supporting single field
    int fieldp = 0;
    struct sql_query q;
    sql_parse(schema->sql, &q);
    char *field_tok = strtok(q.fields, ",");
    while (field_tok && strcmp(field_tok, query->fields)) {
        fieldp++;
        field_tok = strtok(NULL, ",");
    }

    if (!field_tok) {
        fprintf(stderr, "table %s: column '%s' not found\n", query->table,
                query->fields);
        return -1;
    }

    // find fields position
    for (row = 0; row < row_count; row++) {
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, page_header, row, database_file)) {
            fputs("failed to parse table page", stderr);
            break;
        }

        if (fieldp >= cell.record.fields_count) {
            fputs("field parsing failed: field index out of bounds", stderr);
            btree_tleaf_cell_free(&cell);
            return -1;
        }

        struct field *f = &cell.record.fields[fieldp];
        if (f->type == FIELD_TYPE_TEXT) {
            printf("%s\n", f->data);
        } else if (f->type == FIELD_TYPE_NUMBER) {
            printf("%ld\n", f->number);
        }
        btree_tleaf_cell_free(&cell);
    }
    return 0;
}

int sqlite_sql_stmt_exec(struct schema_record *schema, struct sql_query *query,
                         struct db *db, FILE *database_file) {

    struct btree_header page_header;
    int result = 0;
    fseek(database_file, (schema->rootpage - 1) * db->page_size, SEEK_SET);
    btree_header_read(&page_header, 0, database_file);
    if (query->command == COMMAND_SELECT_COUNT) {
        sqlite_sql_stmt_exec_count(&page_header);
    } else if (query->command == COMMAND_SELECT) {
        sqlite_sql_stmt_exec_select(schema, &page_header, query, database_file);
    }

free_header:
    btree_header_free(&page_header);
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
