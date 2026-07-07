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
            free(schema_fields);
            return -1;
        }
        i++;
        free(schema_fields);
    } while ((target_field = strtok_r(NULL, ",", &target_tokptr)));
    return 0;
}

static int sqlite_sql_stmt_exec_select(struct schema_record *schema,
                                       struct btree_header *page_header,
                                       struct sql_query *query,
                                       FILE *database_file) {
    size_t row_count = page_header->cells_count;
    int result = 0;
    int row = 0;

    int fieldp[SELECT_MAX_FIELD_COUNT];
    char *sql = strdup(schema->sql);

    struct sql_query schema_query;
    sql_parse(sql, &schema_query);

    if (!sql) {
        perror("error parsing schema");
        return -1;
    }
    if (sqlite_find_fields(&schema_query, query->fields, fieldp)) {
        free(sql);
        return -1;
    }

    int wfieldp[SELECT_MAX_FIELD_COUNT];
    char **conditions = calloc(schema_query.fields_count, sizeof(void *));
    if (query->command & COMMAND_SELECT_WHERE) {

        struct sql_query schema_ddl;
        sql_parse(schema->sql, &schema_ddl);
        if (sqlite_find_fields(&schema_ddl, query->where_fields_list,
                               wfieldp)) {
            result = -1;
            goto dealloc;
        }

        // create inverted field-index to condition
        // if a field is not conditioned, the pointer will be NULL
        for (int i = 0; i < query->where_fields_count; i++) {
            conditions[wfieldp[i]] = query->where_values[i];
        }
    }

    // find fields position
    for (row = 0; row < row_count; row++) {
        char print_row[1024];
        int print_offset = 0;
        bool filtered = 0;
        struct btree_tleaf_cell cell;
        if (btree_tleaf_cell_read(&cell, page_header, row, database_file)) {
            fputs("failed to parse table page", stderr);
            break;
        }
        for (int i = 0; i < query->where_fields_count; i++) {
            int fp = wfieldp[i];
            struct field *f = &cell.record.fields[fp];
            if (f->type == FIELD_TYPE_TEXT) {
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

        for (int i = 0; i < query->fields_count; i++) {
            int fp = fieldp[i];
            if (i > 0) {
                print_row[print_offset] = '|';
                print_offset++;
            }

            if (fp < 0 || fp >= cell.record.fields_count) {
                fprintf(stderr,
                        "field parsing failed: field index %d out of bounds\n",
                        fp);
                btree_tleaf_cell_free(&cell);
                result = -1;
                goto dealloc;
            }

            struct field *f = &cell.record.fields[fp];
            int c = 0;
            if (f->type == FIELD_TYPE_TEXT) {
                c = snprintf(print_row + print_offset,
                             sizeof print_row - print_offset, "%s", f->data);

            } else if (f->type == FIELD_TYPE_NUMBER) {
                c = snprintf(print_row + print_offset,
                             sizeof print_row - print_offset, "%ld", f->number);
            }
            print_offset += c;
            if (c == 0 || c >= sizeof print_row - print_offset) {
                fputs("results too big\n", stderr);
                btree_tleaf_cell_free(&cell);
                result = -1;
                goto dealloc;
            }
        }
        if (!filtered) {
            puts(print_row);
        }

        btree_tleaf_cell_free(&cell);
    }
dealloc:
    free(sql);
    return result;
}

int sqlite_sql_stmt_exec(struct schema_record *schema, struct sql_query *query,
                         struct db *db, FILE *database_file) {

    struct btree_header page_header;
    int result = 0;
    fseek(database_file, (schema->rootpage - 1) * db->page_size, SEEK_SET);
    btree_header_read(&page_header, 0, database_file);
    if (query->command & COMMAND_SELECT_COUNT) {
        sqlite_sql_stmt_exec_count(&page_header);
    } else if (query->command & COMMAND_SELECT) {
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
