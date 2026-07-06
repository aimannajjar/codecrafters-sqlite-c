#include "sql.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

void strtolower(char *s) {
    char *c = s;
    while (*c) {
        *c = tolower(*c);
        c++;
    }
}

int sql_create_spec_parse(char *spec, struct sql_query *query) {
    // for now we're only interested in field names and not types
    query->fields[0] = 0;
    if (spec[0] && spec[0] == '(')
        spec++;

    char *tok = strtok(spec, " ");
    if (!tok)
        goto invalid;

    int i = 0;
    int offset = 0;
    do {
        if (!strcmp(tok, "(") || !strcmp(tok, ")"))
            continue;


        if (i++ & 1)
            // this is a field type
            continue;

        const char *fmt = (i-1 == 0) ? "%s" : ",%s";

        strtolower(tok);
        int c = snprintf(query->fields + offset, sizeof query->fields - offset,
                         fmt, tok);
        if (c < 0 || c >= sizeof query->fields - offset) {
            fputs("error building field list or field list too big", stderr);
            goto invalid;
        }
        offset += c;

    } while ((tok = strtok(NULL, " ")));

    return 0;
invalid:
    return -1;
}
/** very simplistic SQL parser supports only few types of statements
 ** this will modify *sql
 */
int sql_parse(char *sql, struct sql_query *query) {

    char *tok;
    int i = 0;
    tok = strtok(sql, " ");
    query->table[0] = '\0';
    query->command = COMMAND_INVALID;

    if (!tok)
        goto invalid;

    do {
        strtolower(tok);
        switch (i++) {
        case 0:
            if (strcmp(tok, "select") == 0) {
                query->command = COMMAND_SELECT;
            } else if (strcmp(tok, "create") == 0) {
                query->command = COMMAND_CREATE;
            }
            break;
        case 1:
            if (query->command == COMMAND_SELECT) {
                if (strcmp(tok, "count(*)") == 0) {
                    query->command = COMMAND_SELECT_COUNT;
                } else {
                    strncpy(query->fields, tok, FIELDS_LIST_MAX_LEN - 1);
                    query->fields[sizeof query->fields - 1] = 0;
                }
            } else if (query->command == COMMAND_CREATE) {
                if (strcmp(tok, "table") != 0) {
                    // we only support create table so far
                    printf("B\n");
                    goto invalid;
                }
            }
            break;
        case 2:
            if (query->command == COMMAND_CREATE) {
                strncpy(query->table, tok, TABLE_NAME_MAX_LEN);
                query->table[sizeof query->table - 1] = '\0';
            }
            break;
        case 3:
            if (query->command == COMMAND_CREATE) {
                // consume all remaining tokens to put hem in field list
                char *create_spec = strtok(NULL, "");
                if (sql_create_spec_parse(create_spec, query)) {
                    printf("C\n");
                    goto invalid;
                }
            } else if (query->command & (COMMAND_SELECT | COMMAND_SELECT_COUNT |
                                         COMMAND_SELECT_WHERE)) {
                strncpy(query->table, tok, TABLE_NAME_MAX_LEN);
                query->table[sizeof query->table - 1] = '\0';
            }
            break;
        }
    } while ((tok = strtok(NULL, " ")));

    if (query->table[0] == '\0' || query->command == COMMAND_INVALID) {
    invalid:
        fputs("invalid SQL statement\n", stderr);
        return -1;
    }

    return 0;
}
