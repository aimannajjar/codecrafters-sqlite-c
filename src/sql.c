#include "sql.h"
#include "sqlite.h"
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

#define KEYWORD_MATCH(token, keyword)                                          \
    (!strcmp(keyword, token) || !strcmp(keyword ",", token))

static inline bool is_sql_keyword(char *tok) {
    return (KEYWORD_MATCH(tok, "primary") || KEYWORD_MATCH(tok, "key"));
}

int sql_create_spec_parse(char *spec, struct sql_query *query) {
    // for now we're only interested in field names and not types
    query->fields[0] = 0;
    query->fields_count = 0;
    char *tok = strtok(spec, " ,()");
    if (!tok)
        goto invalid;

    int i = 0;
    int offset = 0;
    do {
        strtolower(tok);
        if (is_sql_keyword(tok))
            continue;

        if (i++ & 1)
            // this is a field type
            continue;

        const char *fmt = (i - 1 == 0) ? "%s" : ",%s";

        int c = snprintf(query->fields + offset, sizeof query->fields - offset,
                         fmt, tok);
        if (c < 0 || c >= sizeof query->fields - offset) {
            fputs("error building field list or field list too big", stderr);
            goto invalid;
        }
        offset += c;
        query->fields_count++;

    } while ((tok = strtok(NULL, " ,()")));

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
    query->fields[0] = '\0';
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
            if (query->command & COMMAND_SELECT) {
                if (strcmp(tok, "count(*)") == 0) {
                    query->command = COMMAND_SELECT_COUNT;
                } else {
                    // consume all tokens until "from"
                    int offset = 0;
                    int j = 0;
                    query->fields_count = 0;
                    do {
                        if (!strcmp(tok, "from")) {
                            i++; // we've already consumed extra "from" token
                            break;
                        }

                        char *ptr = NULL;
                        char *field_token = strtok_r(tok, ",", &ptr);
                        if (!field_token)
                            goto invalid;

                        do {

                            char *fmt =
                                (j++ == 0 || query->fields[offset - 1] == ',')
                                    ? "%s"
                                    : ",%s";

                            int c = snprintf(query->fields + offset,
                                             sizeof query->fields - offset, fmt,
                                             field_token);
                            query->fields_count++;

                            if (c <= 0 || c >= sizeof query->fields - offset) {
                                fputs("error building field list or field list "
                                      "too "
                                      "big",
                                      stderr);
                                return -1;
                            }
                            offset += c;
                        } while ((field_token = strtok_r(NULL, ",", &ptr)));

                    } while ((tok = strtok(NULL, " ")));
                    query->fields[sizeof query->fields - 1] = 0;
                }
            } else if (query->command & COMMAND_CREATE) {
                if (strcmp(tok, "table") != 0) {
                    // we only support create table so far
                    fputs("only create table supported currently", stderr);
                    goto invalid;
                }
            }
            break;
        case 2:
            if (query->command & COMMAND_CREATE) {
                strncpy(query->table, tok, TABLE_NAME_MAX_LEN);
                query->table[sizeof query->table - 1] = '\0';
            }
            break;
        case 3:
            if (query->command & COMMAND_CREATE) {
                // consume all remaining tokens to put hem in field list
                char *create_spec_tokens = strtok(NULL, "");
                char create_spec[FIELDS_LIST_MAX_LEN];
                int c = snprintf(create_spec, sizeof create_spec, "%s %s", tok,
                                 create_spec_tokens);
                if (c <= 0 || c >= sizeof create_spec) {
                    fputs("error parsing create sql stmt", stderr);
                    goto invalid;
                }

                if (sql_create_spec_parse(create_spec, query)) {
                    fputs("error parsing create sql stmt", stderr);
                    goto invalid;
                }
            } else if (query->command & (COMMAND_SELECT | COMMAND_SELECT_COUNT |
                                         COMMAND_SELECT_WHERE)) {
                strncpy(query->table, tok, TABLE_NAME_MAX_LEN);
                query->table[sizeof query->table - 1] = '\0';
            }
            break;
        case 4:
            if (query->command & COMMAND_SELECT) {
                // assume single where clause condition
                char *rest = strtok(NULL, "");
                if (!rest) {
                    fputs("parsing error", stderr);
                    return -1;
                }

                char *where_tok = strtok(rest, "= '");
                if (!where_tok) {
                    fputs("where clause parsing error", stderr);
                    return -1;
                }

                int offset = 0;
                int i = 0;
                do {
                    strncpy(query->where_fields[i], where_tok,
                            sizeof query->where_fields[i] - 1);
                    query->where_fields[i][sizeof query->where_fields[i] - 1] =
                        0;

                    // for functions that search the field list as a string
                    char *fmt = (i == 0) ? "%s" : ",%s";
                    int c = snprintf(query->where_fields_list,
                                     sizeof query->where_fields_list - offset,
                                     fmt, where_tok);
                    if (c <= 0 ||
                        c >= sizeof query->where_fields_list - offset) {
                        fputs("error while building filed list", stderr);
                        return -1;
                    }
                    offset += c;

                    where_tok = strtok(NULL, "= '");
                    if (!where_tok) {
                        fputs("expected value", stderr);
                        return -1;
                    }

                    strncpy(query->where_values[i], where_tok,
                            sizeof query->where_values[i] - 1);
                    query->where_values[i][sizeof query->where_values[i] - 1] =
                        0;
                    i++;

                } while ((where_tok = strtok(NULL, "= '")));
                query->where_fields_count = i;
                query->command |= COMMAND_SELECT_WHERE;
            }
            break;
        }
    } while ((tok = strtok(NULL, " ")));

    if (query->table[0] == '\0' || query->command & COMMAND_INVALID) {
        fprintf(stderr,
                "could not determine target table or sql command: %s (%d)\n",
                query->table, query->command);
    invalid:
        fputs("invalid SQL statement\n", stderr);
        return -1;
    }

    return 0;
}
