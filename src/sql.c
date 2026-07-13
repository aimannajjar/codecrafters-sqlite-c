#include "sql.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum sql_token_type {
    TOKEN_STAR,
    TOKEN_EQUAL,
    TOKEN_STRING,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,

    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_COUNT,
    TOKEN_CREATE,
    TOKEN_TABLE,
    TOKEN_IDENTIFIER,

    TOKEN_END,
    TOKEN_INVALID,
};

struct sql_token {
    const char *start;
    enum sql_token_type type;
    size_t length;
};

struct sql_lexer {
    const char *start;
    const char *current;
};

#define CHAR_LOWER(c) ((c) | 0x20)
#define IS_ALPHA(c)                                                            \
    ({                                                                         \
        char a = CHAR_LOWER(c);                                                \
        a >= 'a' && a <= 'z';                                                  \
    })

static char sql_lex_peek(struct sql_lexer *lexer) { return *lexer->current; }

static char sql_lex_peek_next(struct sql_lexer *lexer) {
    return *lexer->current != '\0' ? lexer->current[1] : '\0';
}

static char sql_lex_advance(struct sql_lexer *lexer) {
    lexer->current++;
    return lexer->current[-1];
}

struct sql_token sql_lex_tokenize(struct sql_lexer *lexer,
                                  enum sql_token_type type) {

    struct sql_token tok = {.type = type,
                            .start = lexer->start,
                            .length = lexer->current - lexer->start};
    return tok;
}

static struct sql_token sql_lex_match_keyword_part(struct sql_lexer *lexer,
                                                   size_t start, size_t len,
                                                   const char *part,
                                                   enum sql_token_type type) {
    if (lexer->current - lexer->start == start + len &&
        memcmp(lexer->start, part, len)) {

        return sql_lex_tokenize(lexer, type);
    }
    return sql_lex_tokenize(lexer, TOKEN_IDENTIFIER);
}

static struct sql_token sql_lexer_invalid_token(struct sql_lexer *lexer,
                                                const char *error) {
    lexer->start = error;
    lexer->current = error + strlen(error);
    return sql_lex_tokenize(lexer, TOKEN_INVALID);
}

static bool sql_lex_eof(struct sql_lexer *lexer) {
    return *lexer->current == '\0';
}

static struct sql_token sql_lex_match_string(struct sql_lexer *lexer,
                                             char quote_type) {
    sql_lex_advance(lexer); // skip quote
    while (!sql_lex_eof(lexer) && sql_lex_peek(lexer) != quote_type)
        sql_lex_advance(lexer);

    if (*lexer->current != quote_type)
        return sql_lexer_invalid_token(lexer, "unterminated string");

    return sql_lex_tokenize(lexer, TOKEN_STRING);
}

static struct sql_token sql_lex_match_identifier(struct sql_lexer *lexer) {
    while (!sql_lex_eof(lexer) && IS_ALPHA(sql_lex_peek(lexer)))
        sql_lex_advance(lexer);

    // was it a keyword?
    switch (*lexer->start) {
    case 's':
        return sql_lex_match_keyword_part(lexer, 1, 5, "elect", TOKEN_SELECT);
    case 'f':
        return sql_lex_match_keyword_part(lexer, 1, 3, "rom", TOKEN_SELECT);
    case 'w':
        return sql_lex_match_keyword_part(lexer, 1, 4, "here", TOKEN_WHERE);
    case 't':
        return sql_lex_match_keyword_part(lexer, 1, 4, "able", TOKEN_TABLE);
    case 'c':
        if (lexer->current - lexer->start > 1) {
            switch (lexer->start[1]) {
            case 'o':
                return sql_lex_match_keyword_part(lexer, 2, 3, "unt",
                                                  TOKEN_COUNT);
            case 'r':
                return sql_lex_match_keyword_part(lexer, 2, 4, "eate",
                                                  TOKEN_CREATE);
            }
        }
    }

    return sql_lex_tokenize(lexer, TOKEN_IDENTIFIER);
}

struct sql_token sql_lex_scan_token(struct sql_lexer *lexer) {
#define SIMPLE_MATCH(C, TYPE)                                                  \
    case (C):                                                                  \
        return sql_lex_tokenize(lexer, TYPE);

    if (*lexer->current == 0 || *lexer->current == ';')
        return sql_lex_tokenize(lexer, TOKEN_END);

    while (*lexer->current == ' ' || *lexer->current == '\t' ||
           *lexer->current == '\n')
        sql_lex_advance(lexer);

    lexer->start = lexer->current;

    char c = sql_lex_advance(lexer);

    // clang-format off
    if (IS_ALPHA(c))
        return sql_lex_match_identifier(lexer);

    switch (c) {
    SIMPLE_MATCH('*', TOKEN_STAR)
    SIMPLE_MATCH('=', TOKEN_EQUAL)
    SIMPLE_MATCH('(', TOKEN_LEFT_PAREN)
    SIMPLE_MATCH(')', TOKEN_RIGHT_PAREN)
    SIMPLE_MATCH(',', TOKEN_COMMA)
    case '"':
    case '\'':
        return sql_lex_match_string(lexer, *lexer->current);
    }
    // clang-format on

#undef SIMPLE_MATCH
    return sql_lex_tokenize(lexer, TOKEN_INVALID);
}

struct sql_parser {
    struct sql_token current;
    struct sql_token previous;
    struct sql_lexer *lexer;
    const char *source;
};

static void sql_parse_error(struct sql_parser *parser, struct sql_query *q,
                            const char *msg) {
    sprintf(q->parse_error_string, "error at %.*s: %s",
            (int)parser->previous.length, parser->previous.start, msg);
    q->parse_error = true;
}

static void sql_parse_advance(struct sql_parser *parser) {
    parser->previous = parser->current;
    parser->current = sql_lex_scan_token(parser->lexer);
}

static void sql_parse_select_field_list(struct sql_parser *parser,
                                        struct sql_query *q) {

    switch (parser->previous.type) {
    case TOKEN_STAR:
        q->type = SQL_SELECT_STATEMENT;
        q->fieldsn[q->fields_count++] = (struct sql_field){
            .field_name = "*",
            .field_len = 1,
        };
        break;
    case TOKEN_IDENTIFIER:
        while (1) {
            q->fieldsn[q->fields_count++] = (struct sql_field){
                .field_name = parser->previous.start,
                .field_len = parser->previous.length,
            };
            sql_parse_advance(parser);
            if (parser->previous.type != TOKEN_COMMA)
                break;
            sql_parse_advance(parser); // skip the comma
            if (parser->previous.type != TOKEN_IDENTIFIER)
                return sql_parse_error(parser, q, "expected identifier");
        }

    default:
    }
}

static void sql_parse_select(struct sql_parser *parser, struct sql_query *q) {
    sql_parse_advance(parser);

    // parse field list
    sql_parse_select_field_list(parser, q);
    if (q->parse_error)
        return;
}

static void sql_parse_create(struct sql_parser *parser, struct sql_query *q) {}

static void sql_parse_sql_stmt(struct sql_parser *parser, struct sql_query *q) {
    sql_parse_advance(parser);
    switch (parser->previous.type) {
    case TOKEN_SELECT:
        return sql_parse_select(parser, q);
    case TOKEN_CREATE:
        return sql_parse_create(parser, q);
    default:
        return sql_parse_error(parser, q, "expected SQL verb");
    }
}

struct sql_query sql_parse_new(const char *query) {
    struct sql_lexer lexer = {
        .start = query,
        .current = query,
    };

    struct sql_parser parser = {
        .current = NULL,
        .previous = NULL,
        .lexer = &lexer,
        .source = query,
    };

    struct sql_query q = {
        .fields_count = 0,
        .parse_error = false,
    };

    sql_parse_advance(&parser);
    sql_parse_sql_stmt(&parser, &q);

    return q;
}

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
    return (KEYWORD_MATCH(tok, "primary") || KEYWORD_MATCH(tok, "key") ||
            KEYWORD_MATCH(tok, "autoincrement") || KEYWORD_MATCH(tok, "not") ||
            KEYWORD_MATCH(tok, "null"));
}

int sql_create_spec_parse(char *spec, struct sql_query *query) {
    // for now we're only interested in field names and not types
    query->fields_list[0] = 0;
    query->fields_count = 0;
    char *tok = strtok(spec, " \t\n,()");
    if (!tok)
        goto invalid;

    int i = 0;
    int offset = 0;
    do {
        char field_name[FIELD_NAME_MAX_LEN];
        int foffset = 0;
        strtolower(tok);
        // printf("tok is '%s'\n", tok);
        if (KEYWORD_MATCH(tok, "autoincrement")) {
        }

        if (is_sql_keyword(tok))
            continue;

        if (i++ & 1)
            // this is a field type
            continue;

        if (tok[0] == '"') {
            // skip leading double-quote
            strncpy(field_name, tok + 1, sizeof field_name);
            foffset = strlen(tok) - 1;
            while ((tok = strtok(NULL, " ")) && tok[strlen(tok) - 1] != '"') {
                // TODO error handling
                foffset += snprintf(field_name + foffset,
                                    sizeof(field_name) - foffset, " %s", tok);
            }
            // strip ending double-quote
            tok[strlen(tok) - 1] = 0;
        }

        snprintf(field_name + foffset, sizeof(field_name) - foffset,
                 foffset ? " %s" : "%s", tok);

        const char *fmt = (i - 1 == 0) ? "%s" : ",%s";

        int c = snprintf(query->fields_list + offset,
                         sizeof query->fields_list - offset, fmt, field_name);
        if (c < 0 || c >= sizeof query->fields_list - offset) {
            fputs("error building field list or field list too big", stderr);
            goto invalid;
        }
        offset += c;
        query->fields_count++;

    } while ((tok = strtok(NULL, " \t\n,()")));

    return 0;
invalid:
    return -1;
}
/** very simplistic SQL parser supports only few types of statements
 ** this will modify *sql
 */
int sql_parse(char *sql, struct sql_query *query) {

    // printf("parsing %s\n", sql);
    char *tok;
    int i = 0;
    tok = strtok(sql, " \n");
    query->table[0] = '\0';
    query->fields_list[0] = '\0';
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
                query->command = COMMAND_CREATE_TABLE;
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
                        strtolower(tok);
                        if (!strcmp(tok, "from")) {
                            i++; // we've already consumed extra "from"
                                 // token
                            break;
                        }

                        char *ptr = NULL;
                        char *field_token = strtok_r(tok, ",", &ptr);
                        if (!field_token)
                            goto invalid;

                        do {

                            char *fmt = (j++ == 0 ||
                                         query->fields_list[offset - 1] == ',')
                                            ? "%s"
                                            : ",%s";

                            int c = snprintf(query->fields_list + offset,
                                             sizeof query->fields_list - offset,
                                             fmt, field_token);
                            strncpy(query->fields[query->fields_count],
                                    field_token, sizeof query->fields[0]);
                            query->fields[query->fields_count]
                                         [sizeof query->fields[0] - 1] = '\0';
                            query->fields_count++;

                            if (c <= 0 ||
                                c >= sizeof query->fields_list - offset) {
                                fputs("error building field list or field "
                                      "list "
                                      "too "
                                      "big",
                                      stderr);
                                return -1;
                            }
                            offset += c;
                        } while ((field_token = strtok_r(NULL, ",", &ptr)));

                    } while ((tok = strtok(NULL, " ")));
                    query->fields_list[sizeof query->fields_list - 1] = 0;
                }
            } else if (query->command & COMMAND_CREATE_TABLE) {
                if (!strcmp(tok, "index")) {
                    query->command = COMMAND_CREATE_INDEX;
                } else if (strcmp(tok, "table")) {
                    // we only support create table so far
                    fprintf(stderr,
                            "only create table supported currently, got: %s\n",
                            tok);
                    goto invalid;
                }
            }
            break;
        case 2:
            if (query->command &
                (COMMAND_CREATE_TABLE | COMMAND_CREATE_INDEX)) {
                strncpy(query->table, tok, TABLE_NAME_MAX_LEN);
                query->table[sizeof query->table - 1] = '\0';
            }
            break;
        case 3:
            if (query->command & COMMAND_CREATE_TABLE) {
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

                    where_tok = strtok(NULL, "='");
                    where_tok = strtok(NULL, "='");

                    if (!where_tok) {
                        fputs("expected value", stderr);
                        return -1;
                    }

                    strncpy(query->where_values[i], where_tok,
                            sizeof query->where_values[i] - 1);
                    query->where_values[i][sizeof query->where_values[i] - 1] =
                        0;
                    i++;

                } while ((where_tok = strtok(NULL, "='")));
                query->where_fields_count = i;
                query->command |= COMMAND_SELECT_WHERE;
            }
            break;
        }
    } while ((tok = strtok(NULL, " \n")));

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
