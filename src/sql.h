#ifndef SQL_H
#define SQL_H

#include <stddef.h>
#define TABLE_NAME_MAX_LEN 100
#define FIELDS_LIST_MAX_LEN 1024
#define FIELD_NAME_MAX_LEN 32
#define FIELD_VALUE_TEXT_MAX_LEN 1024
#define SELECT_MAX_FIELD_COUNT 20

#define SQL_STATEMENT_FIELD_MAX_LEN 20
#define SQL_PARSE_ERROR_STRING_MAX 256
#define SQL_STATEMENT_MAX_CONDITIONS_LEN 20

enum SQL_STATEMENT_TYPE {
    SQL_SELECT_STATEMENT,
    SQL_SELECT_COUNT_STATEMENT,
    SQL_CREATE_TABLE_STATEMENT,
    SQL_CREATE_INDEX_STATEMENT,
};

struct sql_field {
    const char *field_name; // a slice from query, not nul-terminated, use len
    size_t field_len;
};

struct sql_select_condition {
    const char *field_name; // a slice from query, not nul-terminated, use len
    size_t field_name_len;

    const char *field_value; // a slice from query, not nul-terminated, use len
    size_t field_value_len;
    bool is_numeric;
};

struct sql_query {
    struct sql_field fields[SQL_STATEMENT_FIELD_MAX_LEN];
    size_t fields_count;

    struct sql_select_condition conditions[SQL_STATEMENT_MAX_CONDITIONS_LEN];
    size_t conditions_count;

    const char *table_name; // a slice from query, not nul-terminated, use len
    size_t table_name_len;

    char parse_error_string[SQL_PARSE_ERROR_STRING_MAX];
    bool parse_error;

    enum SQL_STATEMENT_TYPE type;
};

struct sql_query sql_parse(const char *query);

#endif // SQL_H
