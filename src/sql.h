#ifndef SQL_H
#define SQL_H

#define TABLE_NAME_MAX_LEN 100
#define FIELDS_LIST_MAX_LEN 1024
#define FIELD_NAME_MAX_LEN 32
#define FIELD_VALUE_TEXT_MAX_LEN 1024
#define SELECT_MAX_FIELD_COUNT 20

enum SQL_COMMAND {
    COMMAND_INVALID = 0,
    COMMAND_SELECT_COUNT = 2,
    COMMAND_INSERT = 4,
    COMMAND_SELECT = 8,
    COMMAND_SELECT_WHERE = 16,
    COMMAND_CREATE = 32,
};

struct sql_query {
    char table[TABLE_NAME_MAX_LEN];
    char fields[SELECT_MAX_FIELD_COUNT][FIELDS_LIST_MAX_LEN];
    char fields_list[FIELDS_LIST_MAX_LEN];
    int fields_count;
    char where_fields_list[FIELDS_LIST_MAX_LEN];
    char where_fields[SELECT_MAX_FIELD_COUNT][FIELD_NAME_MAX_LEN];
    char where_values[SELECT_MAX_FIELD_COUNT][FIELD_VALUE_TEXT_MAX_LEN];
    int where_fields_count;
    enum SQL_COMMAND command;
};

int sql_parse(char *sql, struct sql_query *query);

#endif // SQL_H
