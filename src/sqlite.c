#include "sqlite.h"
#include "database.h"
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
    if ((rlen = read_schema_table(db, &records, database_file)) < 0) {
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

static int sqlite_sql_stmt_exec_count(struct db *db,
                                      struct btree_header *page_header,
                                      FILE *database_file) {
    if (page_header->page_type == TABLE_LEAF_PAGE)
        return page_header->cells_count;

    size_t child_count = page_header->cells_count;
    int count = 0;
    for (int i = 0; i < child_count; i++) {
        struct btree_header header;
        uint64_t pn = btree_tinterior_cell_read(page_header, i, database_file);
        btree_header_read(db, &header, pn, database_file);
        count += sqlite_sql_stmt_exec_count(db, &header, database_file);
        btree_header_free(&header);
    }

    return count;
}

/** Given a create sql statemnt and a list of desired fields
 ** find the requested fields index in the create statement
 ** and populate field_results with the index, or -1 if not found
 **/
static int
sqlite_find_fields(struct sql_query *schema_query,
                   // TODO: Make this an array instead of comma-separated list
                   char *target_fields,
                   int field_results[SELECT_MAX_FIELD_COUNT]) {

    char *target_tokptr = NULL;
    char *target_field = strtok_r(target_fields, ",", &target_tokptr);
    if (!target_field) {
        fputs("field list empty", stderr);
        return -1;
    }
    // simplistic nested search , room for optimzation
    int i = 0;
    do {
        if (i >= SELECT_MAX_FIELD_COUNT) {
            fputs("field list too big", stderr);
            return -1;
        }
        field_results[i] = -1;

        // iterate through the create table schema to find
        // the index of each requested field
        // make copy of the fields to allow multiple searches
        char *schema_ptr = NULL;
        char *schema_fields = strdup(schema_query->fields);
        if (!schema_fields) {
            perror("error searching fields list");
            return -1;
        }

        // tokenize fields list
        char *schema_field = strtok_r(schema_fields, ",", &schema_ptr);
        int j = 0;
        if (!schema_field) {
            free(schema_fields);
            fputs("empty field list in create schema\n", stderr);
            return -1;
        }

        // search lineraly
        do {
            if (!strcmp(target_field, schema_field)) {
                field_results[i] = j;
                break;
            }
            j++;
        } while ((schema_field = strtok_r(NULL, ",", &schema_ptr)));

        // if fieldp default -1 value hasn't changed it means we haven't
        // the field in create schema
        if (field_results[i] == -1) {
            fprintf(stderr, "column '%s' not found\n", target_field);
            fprintf(stderr, "known fields are: %s\n", schema_query->fields);
            free(schema_fields);
            return -1;
        }
        i++;
        free(schema_fields);
    } while ((target_field = strtok_r(NULL, ",", &target_tokptr)));
    return 0;
}

static int sqlite_sql_stmt_exec_select(char **conditions, int *wfieldp,
                                       int *fieldp,
                                       struct btree_header *page_header,
                                       struct sql_query *query,
                                       FILE *database_file) {

    int result = 0;

    size_t row_count = page_header->cells_count;
    int row = 0;

    for (row = 0; row < row_count; row++) {
        bool filtered = 0;
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, page_header, row, database_file)) {
            fputs("failed to parse table page\n", stderr);
            break;
        }

        if (!cell.record.fields_count) {
            // skipping empty cell
            btree_tleaf_cell_free(&cell);
            continue;
        }

        if (query->command & COMMAND_SELECT_WHERE) {
            printf("QUEY TYPE IS COR\n");
            for (int i = 0; i < query->where_fields_count; i++) {
                int fp = wfieldp[i];
                struct field *f = &cell.record.fields[fp];
                if (f->type == FIELD_TYPE_TEXT) {
                    printf("field %d (%s) should = %s\n", fp, f->data,
                           conditions[fp]);
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
            int fp = fieldp[i];
            if (i > 0)
                putchar('|');

            if (fp < 0 || fp >= cell.record.fields_count) {
                fprintf(stderr,
                        "field parsing failed: field index %d out of bounds\n",
                        fp);
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

int sqlite_sql_stmt_exec_select_interiors(char **conditions, int *wfieldp,
                                          int *fieldp, struct sql_query *query,
                                          struct btree_header *parent_page,
                                          struct db *db, FILE *database_file) {
    size_t child_count = parent_page->cells_count;

    for (int i = 0; i < child_count; i++) {
        struct btree_header header;
        uint64_t pn = btree_tinterior_cell_read(parent_page, i, database_file);
        btree_header_read(db, &header, pn, database_file);
        sqlite_sql_stmt_exec_select(conditions, wfieldp, fieldp, &header, query,
                                    database_file);
    }

    return 0;
}

int sqlite_sql_stmt_exec(struct schema_record *schema, struct sql_query *query,
                         struct db *db, FILE *database_file) {

    struct btree_header page_header;
    int result = 0;

    btree_header_read(db, &page_header, schema->rootpage, database_file);
    if (query->command & COMMAND_SELECT_COUNT) {
        int count = sqlite_sql_stmt_exec_count(db, &page_header, database_file);
        printf("%d\n", count);
    } else if (query->command & COMMAND_SELECT) {
        struct sql_query schema_query;
        int fieldp[SELECT_MAX_FIELD_COUNT];
        int wfieldp[SELECT_MAX_FIELD_COUNT];
        char **conditions = NULL;
        char *sql = strdup(schema->sql);

        sql_parse(sql, &schema_query);

        if (!sql) {
            perror("error parsing schema");
            return -1;
        }
        if (sqlite_find_fields(&schema_query, query->fields, fieldp)) {
            free(sql);
            return -1;
        }

        if (query->command & COMMAND_SELECT_WHERE) {
            conditions = calloc(schema_query.fields_count, sizeof(void *));

            struct sql_query schema_ddl;
            sql_parse(schema->sql, &schema_ddl);
            if (sqlite_find_fields(&schema_ddl, query->where_fields_list,
                                   wfieldp)) {
                result = -1;
                goto free_header;
            }

            // create inverted field-index to condition
            // if a field is not conditioned, the pointer will be NULL
            for (int i = 0; i < query->where_fields_count; i++) {
                conditions[wfieldp[i]] = query->where_values[i];
            }
        }

        if (page_header.page_type == TABLE_INTERIOR_PAGE) {
            sqlite_sql_stmt_exec_select_interiors(conditions, wfieldp, fieldp,
                                                  query, &page_header, db,
                                                  database_file);
        } else {
            sqlite_sql_stmt_exec_select(conditions, wfieldp, fieldp,
                                        &page_header, query, database_file);
        }
    }

free_header:
    btree_header_free(&page_header);
    return result;
}

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file) {
    struct btree_header schema_page_header;

    if (read_schema_page_header(db, &schema_page_header, database_file)) {
        return -1;
    }

    printf("database page size: %u\n", (unsigned)db->page_size);
    printf("number of tables: %u\n", (unsigned)schema_page_header.cells_count);

    return 0;
}

int sqlite_cmd_tables(struct db *db, FILE *database_file) {
    struct schema_record *records;
    int rlen;
    if ((rlen = read_schema_table(db, &records, database_file)) < 0) {
        return -1;
    }

    for (int i = 0; i < rlen; i++) {
        printf("%s\t", records[i].tbl_name);
    }

    puts("");

    free(records);
    return 0;
}
