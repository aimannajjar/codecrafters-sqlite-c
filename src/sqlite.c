#include "sqlite.h"
#include "database.h"
#include "hashmap.h"
#include "page.h"
#include "sql.h"
#include <stdint.h>
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
    if ((rlen = db_read_schema_table(db, &records, database_file)) < 0) {
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
    printf("FOUBND TABKE\n");
    sqlite_sql_stmt_exec(&records[i], &query, db, database_file);
    free(records);
    return 0;

not_found:
    printf("table '%s' does not exist\n", query.table);
    free(records);
    return -1;
}

static int sqlite_sql_stmt_exec_count(struct db *db,
                                      struct btree_page *page_header,
                                      FILE *database_file) {
    if (page_header->page_type == TABLE_LEAF_PAGE)
        return page_header->cells_count;

    size_t child_count = page_header->cells_count;
    int count = 0;
    for (int i = 0; i < child_count; i++) {
        struct btree_page header;
        uint64_t pn = btree_tinterior_cell_read(page_header, i, database_file);
        btree_page_read(db, &header, pn, database_file);
        count += sqlite_sql_stmt_exec_count(db, &header, database_file);
        btree_page_free(&header);
    }

    return count;
}

static int sqlite_sql_stmt_exec_select_leaf(char **conditions,
                                            struct schema_record *ddl,
                                            struct btree_page *page,
                                            struct sql_query *query,
                                            FILE *database_file) {

    int result = 0;

    size_t row_count = page->cells_count;
    int row = 0;

    for (row = 0; row < row_count; row++) {
        bool filtered = 0;
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, page, row, database_file)) {
            fputs("failed to parse table page\n", stderr);
            break;
        }

        if (!cell.record.fields_count) {
            // skipping empty cell
            btree_tleaf_cell_free(&cell);
            continue;
        }

        if (query->command & COMMAND_SELECT_WHERE) {
            for (int i = 0; i < query->where_fields_count; i++) {
                int fp = hget(&ddl->col_index, query->where_fields[i]);
                printf("field %s (index %d) has condition = %s\n",
                       query->where_fields[i], fp, conditions[fp]);
                printf("Create was \n\t%s\n", ddl->sql);
                struct field *f = &cell.record.fields[fp];
                printf("this row's value for this field is %s\n", f->data);
                if (f->type == FIELD_TYPE_TEXT) {
                    printf("does %s = %s\n", f->data, conditions[fp]);
                    if (conditions[fp] && strcmp(f->data, conditions[fp])) {
                        filtered = 1;
                    }

                } else if (f->type == FIELD_TYPE_NUMBER) {
                    if (conditions[fp]) {
                        int v = atoi(conditions[fp]);
                        if (v != f->number) {
                            filtered = 1;
                            break;
                        }
                    }
                }
            }
            if (filtered) {
                btree_tleaf_cell_free(&cell);
                continue;
            }
        }

        for (int i = 0; i < query->fields_count; i++) {
            int fp = hget(&ddl->col_index, query->fields[i]);
            if (i > 0)
                putchar('|');

            if (fp < 0 || fp >= cell.record.fields_count) {
                fprintf(
                    stderr,
                    "field parsing failed: %s field index %d out of bounds\n",
                    query->fields[i], fp);
                btree_tleaf_cell_free(&cell);
                return -1;
            }

            struct field *f = &cell.record.fields[fp];
            if (f->type == FIELD_TYPE_TEXT) {
                printf("%s", f->data);
            } else if (f->type == FIELD_TYPE_NUMBER) {
                printf("%ld", f->number);
            }
        }
        puts("");

        btree_tleaf_cell_free(&cell);
    }
    return result;
}

int sqlite_sql_stmt_exec_select_interiors(char **conditions,
                                          struct schema_record *ddl,
                                          struct sql_query *query,
                                          struct btree_page *parent_page,
                                          struct db *db, FILE *database_file) {
    size_t child_count = parent_page->cells_count;

    for (int i = 0; i < child_count; i++) {
        struct btree_page header;
        uint64_t pn = btree_tinterior_cell_read(parent_page, i, database_file);
        btree_page_read(db, &header, pn, database_file);
        sqlite_sql_stmt_exec_select_leaf(conditions, ddl, &header, query,
                                         database_file);
        btree_page_free(&header);
    }

    return 0;
}

int sqlite_sql_stmt_exec(struct schema_record *schema, struct sql_query *query,
                         struct db *db, FILE *database_file) {

    // rootpage as found in schema
    struct btree_page rootpage;
    int result = 0;

    printf("CREATE IS:\n\t%s\n", schema->sql);

    btree_page_read(db, &rootpage, schema->rootpage, database_file);
    if (query->command & COMMAND_SELECT_COUNT) {
        int count = sqlite_sql_stmt_exec_count(db, &rootpage, database_file);
        printf("%d\n", count);
    } else if (query->command & COMMAND_SELECT) {
        struct sql_query schema_query;
        char **conditions = NULL;
        char *sql = strdup(schema->sql);

        // parse DDL query to figure out field positions
        sql_parse(sql, &schema_query);

        if (!sql) {
            perror("error parsing schema");
            result = -1;
            goto free_header;
        }

        // if select query has were clause, build conditions array
        if (query->command & COMMAND_SELECT_WHERE) {
            conditions = calloc(schema_query.fields_count, sizeof(void *));

            // create inverted field-index to condition
            // if a field is not conditioned, the pointer will be NULL
            for (int i = 0; i < query->where_fields_count; i++) {
                int n = hget(&schema->col_index, query->where_fields[i]);
                conditions[n] = query->where_values[i];
            }
        }

        if (rootpage.page_type == TABLE_INTERIOR_PAGE) {
            sqlite_sql_stmt_exec_select_interiors(conditions, schema, query,
                                                  &rootpage, db, database_file);
        } else {
            sqlite_sql_stmt_exec_select_leaf(conditions, schema, &rootpage,
                                             query, database_file);
        }
    }

free_header:
    btree_page_free(&rootpage);
    return result;
}

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file) {
    struct btree_page schema_page_header;

    if (db_read_schema_page(db, &schema_page_header, database_file)) {
        return -1;
    }

    printf("database page size: %u\n", (unsigned)db->page_size);
    printf("number of tables: %u\n", (unsigned)schema_page_header.cells_count);

    return 0;
}

int sqlite_cmd_tables(struct db *db, FILE *database_file) {
    struct schema_record *records;
    int rlen;
    if ((rlen = db_read_schema_table(db, &records, database_file)) < 0) {
        return -1;
    }

    for (int i = 0; i < rlen; i++) {
        printf("%s\t", records[i].tbl_name);
    }

    puts("");

    free(records);
    return 0;
}
