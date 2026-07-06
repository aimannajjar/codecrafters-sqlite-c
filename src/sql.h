#ifndef SQL_H
#define SQL_H

#define TABLE_NAME_MAX_LEN 100
#define FIELDS_LIST_MAX_LEN 1024

enum SQL_COMMAND {
    COMMAND_INVALID = -1,
    COMMAND_SELECT_COUNT = 0,
    COMMAND_INSERT = 2,
    COMMAND_SELECT = 4,
    COMMAND_SELECT_WHERE = 8,
    COMMAND_CREATE = 16,
};

struct sql_query {
    char table[TABLE_NAME_MAX_LEN];
    char fields[FIELDS_LIST_MAX_LEN];
    enum SQL_COMMAND command;
};

int sql_parse(char *sql, struct sql_query *query);

#endif // SQL_H
