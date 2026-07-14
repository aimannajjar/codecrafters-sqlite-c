#include "sqlite.h"
#include "database.h"
#include "hashmap.h"
#include "page.h"
#include "sql.h"
#include <assert.h>
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
    int table_ddl = -1;
    int index_ddl = -1;

    struct sql_query query = sql_parse_new(stmt);
    if (query.parse_error) {
        fprintf(stderr, "%s\n", query.parse_error_string);
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
        if (!strcmp(records[i].type, "table") &&
            !memcmp(records[i].tbl_name, query.table_name,
                    query.table_name_len)) {
            table_ddl = i;
        } else if (!strcmp(records[i].type, "index") &&
                   !strcmp(records[i].tbl_name, query.table)) {
            index_ddl = i;
        }

        if (table_ddl >= 0 && index_ddl >= 0)
            break;
    }

    if (table_ddl >= 0) {
        if (query.where_fields_count && index_ddl > 0) {
            sqlite_sql_search_index(db, &records[index_ddl],
                                    &records[table_ddl], &query, database_file);
        } else {
            sqlite_sql_stmt_exec(&records[table_ddl], &query, db,
                                 database_file);
        }
        free(records);
        return 0;
    }

    printf("table '%.*s' does not exist\n", (int)query.table_name_len,
           query.table_name);
    free(records);
    return -1;
}

// TODO: use proper tree traversal
int sqlite_sql_search_index_tree(struct db *db, struct btree_page *page,
                                 char *condition, size_t n, int64_t results[n],
                                 size_t *curr, FILE *database_file) {

    if (*curr >= n) {
        printf("reached maximum results: %lu\n", *curr);
        return -1;
    }

    for (size_t ci = 0; ci < page->cells_count; ci++) {
        struct btree_index_cell cell;
        btree_index_cell_read(&cell, page, ci, database_file);
        int match = 0;
        for (size_t i = 0; i < cell.record.fields_count; i++) {
            struct field *f = &cell.record.fields[i];
            if (f->type == FIELD_TYPE_TEXT) {
                if (!strcmp(condition, f->data)) {
                    match = 1;
                }
            } else if (f->type == FIELD_TYPE_NUMBER) {
                if (match) {
                    results[(*curr)++] = f->number;
                }
            }
        }

        // if this is interior page, search in left ptr
        if (page->page_type == INDEX_INTERIOR_PAGE) {
            struct btree_page left_child;
            btree_page_read(db, &left_child, cell.left_child_pn, database_file);
            if (left_child.page_type == INDEX_INTERIOR_PAGE) {
                sqlite_sql_search_index_tree(db, &left_child, condition, n,
                                             results, curr, database_file);
            } else {
                sqlite_sql_search_index_tree(db, &left_child, condition, n,
                                             results, curr, database_file);
            }
            btree_page_free(&left_child);
        }
        btree_index_cell_free(&cell);
    }
    return 0;
}

int sqlite_sql_search_index(struct db *db, struct schema_record *index_ddl,
                            struct schema_record *table_ddl,
                            struct sql_query *query, FILE *database_file) {
    struct btree_page page;
    int64_t results[SEARCH_MAX_RESULTS];
    size_t found_pos = 0;
    if (btree_page_read(db, &page, index_ddl->rootpage, database_file))
        return -1;

    if (page.page_type == INDEX_INTERIOR_PAGE) {
        sqlite_sql_search_index_tree(db, &page, query->where_values[0],
                                     sizeof results, results, &found_pos,
                                     database_file);

        // search right most child of page
        struct btree_page right_child;
        if (btree_page_read(db, &right_child, page.right_ptr, database_file))
            return -1;

        sqlite_sql_search_index_tree(db, &right_child, query->where_values[0],
                                     sizeof results, results, &found_pos,
                                     database_file);

        btree_page_free(&right_child);
    }

    for (int i = 0; i < found_pos; i++) {
        sqlite_sql_stmt_exec_select_rowid(results[i], table_ddl, query, db,
                                          database_file);
    }

    btree_page_free(&page);
    return 0;
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
        uint64_t pn =
            btree_tinterior_cell_read(page_header, i, NULL, database_file);
        btree_page_read(db, &header, pn, database_file);
        count += sqlite_sql_stmt_exec_count(db, &header, database_file);
        btree_page_free(&header);
    }

    return count;
}

static int sqlite_sql_stmt_exec_select_leaf(struct schema_record *ddl,
                                            struct btree_page *page,
                                            struct sql_query *query,
                                            FILE *database_file) {

    int result = 0;

    size_t row_count = page->cells_count;
    int row = 0;

    for (row = 0; row < row_count; row++) {
        int filtered = 1;
        struct btree_leaf_cell cell;
        if (btree_leaf_cell_read(&cell, page, row, database_file)) {
            fputs("failed to parse table page\n", stderr);
            break;
        }

        if (query->conditions_count) {
            for (int i = 0; i < query->conditions_count; i++) {
                struct sql_select_condition cond = query->conditions[i];
                int fp =
                    hget(&ddl->col_index, cond.field_name_len, cond.field_name);

                if (fp == -1) {
                    fprintf(stderr, "column '%.*s' in where clause not found\n",
                            (int)cond.field_name_len, cond.field_name, stderr);

                    btree_leaf_cell_free(&cell);
                    return -1;
                }

                // currenlty only supporting AND congjunction
                // also only supporting exact match filters for now
                struct field *f = &cell.record.fields[fp];
                if (cond.is_numeric) {
                    int v =
                        strtol(cond.field_value, NULL, cond.field_value_len);
                    if (v != f->number) {
                        filtered = 1;
                        break;
                    }
                } else {
                    if (strncmp(f->data, cond.field_value,
                                cond.field_value_len)) {
                        filtered = 1;
                    } else {
                        filtered = 0;
                    }
                }
            }
            if (filtered) {
                btree_leaf_cell_free(&cell);
                continue;
            }
        }

        for (int i = 0; i < query->fields_count; i++) {
            struct sql_field field = query->fieldsn[i];
            int fp = hget(&ddl->col_index, field.field_len, field.field_name);
            if (i > 0)
                putchar('|');

            if (fp < 0 || fp >= cell.record.fields_count) {
                fprintf(stderr, "select column '%.*s' not found\n",
                        (int)field.field_len, field.field_name);

                btree_leaf_cell_free(&cell);
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

        btree_leaf_cell_free(&cell);
    }

    return result;
}

int sqlite_sql_stmt_exec_select_rowid(uint64_t rowid, struct schema_record *ddl,
                                      struct sql_query *query, struct db *db,
                                      FILE *database_file) {
    uint64_t curr = ddl->rootpage;
    struct btree_page *result_page;
    do {
        struct btree_page page;
        if (btree_page_read(db, &page, curr, database_file))
            return -1;

        // reached leaf page, target rowid must be here or not found
        if (page.page_type == TABLE_LEAF_PAGE) {
            result_page = &page;
            goto found_page;
        }

        for (int i = 0; i < page.cells_count; i++) {
            int64_t crowid;
            uint64_t left_pn =
                btree_tinterior_cell_read(&page, i, &crowid, database_file);
            if (rowid <= crowid) {
                curr = left_pn;
                goto next;
            }
        }

        // try rightmost ptr
        curr = page.right_ptr;
    next:
        btree_page_free(&page);
    } while (1);

found_page:

    for (int row = 0; row < result_page->cells_count; row++) {
        struct btree_leaf_cell cell;
        if (btree_leaf_cell_read(&cell, result_page, row, database_file)) {
            fputs("failed to parse table page\n", stderr);
            break;
        }

        if (cell.rowid != rowid) {
            btree_leaf_cell_free(&cell);
            continue;
        }

        for (int i = 0; i < query->fields_count; i++) {
            int fp = hget(&ddl->col_index, strlen(query->fields[i]),
                          query->fields[i]);
            if (i > 0)
                putchar('|');

            if (fp < 0 || fp >= cell.record.fields_count) {
                fprintf(stderr,
                        "field parsing failed: %s field index %d out of "
                        "bounds\n",
                        query->fields[i], fp);
                btree_leaf_cell_free(&cell);
                return -1;
            }

            struct field *f = &cell.record.fields[fp];
            if (f->type == FIELD_TYPE_TEXT) {
                printf("%s", f->data);
            } else if (f->type == FIELD_TYPE_NUMBER) {
                printf("%ld", f->number);
            }
        }
        btree_leaf_cell_free(&cell);
        puts("");
    }

    btree_page_free(result_page);
    return 0;
}

int sqlite_sql_stmt_exec_select_interiors(struct schema_record *ddl,
                                          struct sql_query *query,
                                          struct btree_page *parent_page,
                                          struct db *db, FILE *database_file) {
    size_t child_count = parent_page->cells_count;

    for (int i = 0; i < child_count; i++) {
        struct btree_page child_page;
        uint64_t pn =
            btree_tinterior_cell_read(parent_page, i, NULL, database_file);

        // TOOD: this logic is redundant with portions of
        // sqlite_sql_stmt_exec
        btree_page_read(db, &child_page, pn, database_file);
        if (child_page.page_type == TABLE_INTERIOR_PAGE) {
            sqlite_sql_stmt_exec_select_interiors(
                ddl, query, &child_page, db, database_file);
        } else if (child_page.page_type == TABLE_LEAF_PAGE) {
            if (sqlite_sql_stmt_exec_select_leaf(ddl, &child_page,
                                                 query, database_file)) {
                btree_page_free(&child_page);
                return -1;
            }
        } else {
            printf("unexpected page type: 0x%02x\n", child_page.page_type);
            btree_page_free(&child_page);
            return -1;
        }

        btree_page_free(&child_page);
    }

    return 0;
}

int sqlite_sql_stmt_exec(struct schema_record *schema, struct sql_query *query,
                         struct db *db, FILE *database_file) {

    // rootpage as found in schema
    struct btree_page rootpage;
    int result = 0;

    btree_page_read(db, &rootpage, schema->rootpage, database_file);
    if (query->type == SQL_SELECT_COUNT_STATEMENT) {
        int count = sqlite_sql_stmt_exec_count(db, &rootpage, database_file);
        printf("%d\n", count);
    } else if (query->type == SQL_SELECT_STATEMENT) {
        char *sql = strdup(schema->sql);

        // parse DDL query to figure out field positions
        struct sql_query schema_query = sql_parse_new(sql);

        if (schema_query.parse_error) {
            fprintf(stderr, "%s\n", schema_query.parse_error_string);
            result = -1;
            goto free_header;
        }

        if (rootpage.page_type == TABLE_INTERIOR_PAGE) {
            sqlite_sql_stmt_exec_select_interiors(schema, query, &rootpage, db,
                                                  database_file);
        } else {
            sqlite_sql_stmt_exec_select_leaf(schema, &rootpage,
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
