#include "sql.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum sql_token_type {
    TOKEN_STAR,
    TOKEN_EQUAL,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,

    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_COUNT,
    TOKEN_CREATE,
    TOKEN_TABLE,
    TOKEN_INDEX,
    TOKEN_AND,
    TOKEN_IDENTIFIER,

    TOKEN_INTEGER,
    TOKEN_TEXT,
    TOKEN_PRIMARY,
    TOKEN_KEY,
    TOKEN_AUTOINCREMENT,
    TOKEN_UNIQUE,
    TOKEN_NOT,
    TOKEN_NULL,

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

#define IS_DIGIT(c)                                                            \
    ({                                                                         \
        char a = c;                                                            \
        a >= '0' && a <= '9';                                                  \
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
    if (lexer->current - lexer->start != start + len)
        return sql_lex_tokenize(lexer, TOKEN_IDENTIFIER);

    while (len--) {
        if (part[len] != CHAR_LOWER(lexer->start[start + len]))
            return sql_lex_tokenize(lexer, TOKEN_IDENTIFIER);
    }

    return sql_lex_tokenize(lexer, type);
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
    sql_lex_advance(lexer); // consume quote mark
    while (!sql_lex_eof(lexer) && sql_lex_peek(lexer) != quote_type)
        sql_lex_advance(lexer);

    if (*lexer->current != quote_type)
        return sql_lexer_invalid_token(lexer, "unterminated string");

    // consume ending quote
    sql_lex_advance(lexer);

    return sql_lex_tokenize(lexer, TOKEN_STRING);
}

static struct sql_token sql_lex_match_number(struct sql_lexer *lexer) {

    while (!sql_lex_eof(lexer) && IS_DIGIT(sql_lex_peek(lexer)))
        sql_lex_advance(lexer);

    return sql_lex_tokenize(lexer, TOKEN_NUMBER);
}

static struct sql_token sql_lex_match_identifier(struct sql_lexer *lexer) {
    while (!sql_lex_eof(lexer) &&
           (IS_ALPHA(sql_lex_peek(lexer)) || IS_DIGIT(sql_lex_peek(lexer)) ||
            sql_lex_peek(lexer) == '_'))
        sql_lex_advance(lexer);

    // was it a keyword?
    switch (CHAR_LOWER(*lexer->start)) {
    case 'a':
        if (lexer->current - lexer->start > 1) {
            switch (CHAR_LOWER(lexer->start[1])) {
            case 'n':
                return sql_lex_match_keyword_part(lexer, 2, 1, "d", TOKEN_AND);
            case 'u':
                return sql_lex_match_keyword_part(lexer, 2, 11, "toincrement",
                                                  TOKEN_AUTOINCREMENT);
            }
        }
        return sql_lex_match_keyword_part(lexer, 1, 2, "nd", TOKEN_AND);
    case 's':
        return sql_lex_match_keyword_part(lexer, 1, 5, "elect", TOKEN_SELECT);
    case 'f':
        return sql_lex_match_keyword_part(lexer, 1, 3, "rom", TOKEN_FROM);
    case 'w':
        return sql_lex_match_keyword_part(lexer, 1, 4, "here", TOKEN_WHERE);
    case 't':
        if (lexer->current - lexer->start > 1) {
            switch (CHAR_LOWER(lexer->start[1])) {
            case 'a':
                return sql_lex_match_keyword_part(lexer, 2, 3, "ble",
                                                  TOKEN_TABLE);
            case 'e':
                return sql_lex_match_keyword_part(lexer, 2, 2, "xt",
                                                  TOKEN_TEXT);
            }
        }
    case 'i':
        if (lexer->current - lexer->start > 2 &&
            CHAR_LOWER(lexer->start[1] == 'n')) {
            switch (CHAR_LOWER(lexer->start[2])) {
            case 'd':
                return sql_lex_match_keyword_part(lexer, 3, 2, "ex",
                                                  TOKEN_INDEX);
            case 't':
                return sql_lex_match_keyword_part(lexer, 3, 4, "eger",
                                                  TOKEN_INTEGER);
            }
        }
        return sql_lex_match_keyword_part(lexer, 1, 6, "nteger", TOKEN_INTEGER);
    case 'p':
        return sql_lex_match_keyword_part(lexer, 1, 6, "rimary", TOKEN_PRIMARY);
    case 'k':
        return sql_lex_match_keyword_part(lexer, 1, 2, "ey", TOKEN_KEY);
    case 'u':
        return sql_lex_match_keyword_part(lexer, 1, 5, "nique", TOKEN_KEY);
    case 'n':
        if (lexer->current - lexer->start > 1) {
            switch (CHAR_LOWER(lexer->start[1])) {
            case 'o':
                return sql_lex_match_keyword_part(lexer, 2, 1, "t",
                                                  TOKEN_NOT);
            case 'u':
                return sql_lex_match_keyword_part(lexer, 2, 2, "ll",
                                                  TOKEN_NULL);
            }
        }
    case 'c':
        if (lexer->current - lexer->start > 1) {
            switch (CHAR_LOWER(lexer->start[1])) {
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

    if (IS_DIGIT(c))
        return sql_lex_match_number(lexer);

    switch (c) {
    SIMPLE_MATCH('*', TOKEN_STAR)
    SIMPLE_MATCH('=', TOKEN_EQUAL)
    SIMPLE_MATCH('(', TOKEN_LEFT_PAREN)
    SIMPLE_MATCH(')', TOKEN_RIGHT_PAREN)
    SIMPLE_MATCH(',', TOKEN_COMMA)
    case '"':
    case '\'':
        return sql_lex_match_string(lexer, c);
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
    if (parser->current.type != TOKEN_END) {
        sprintf(q->parse_error_string, "error at '%.*s': %s",
                (int)parser->current.length, parser->current.start, msg);
    } else {
        sprintf(q->parse_error_string, "error at end: %s", msg);
    }
    q->parse_error = true;
}

static void sql_parse_advance(struct sql_parser *parser) {
    parser->previous = parser->current;
    parser->current = sql_lex_scan_token(parser->lexer);
}

static void sql_parse_select_field_list(struct sql_parser *parser,
                                        struct sql_query *q) {

    sql_parse_advance(parser);

    switch (parser->previous.type) {
    case TOKEN_STAR:
        q->fields[q->fields_count++] = (struct sql_field){
            .field_name = "*",
            .field_len = 1,
        };
        break;
    case TOKEN_IDENTIFIER:
        while (1) {
            q->fields[q->fields_count++] = (struct sql_field){
                .field_name = parser->previous.start,
                .field_len = parser->previous.length,
            };
            if (parser->current.type != TOKEN_COMMA)
                break;
            sql_parse_advance(parser);
            if (parser->current.type != TOKEN_IDENTIFIER)
                return sql_parse_error(parser, q, "expected identifier");
            sql_parse_advance(parser);
        }
        break;
    case TOKEN_COUNT:
        q->type = SQL_SELECT_COUNT_STATEMENT;
        if (parser->current.type != TOKEN_LEFT_PAREN)
            return sql_parse_error(parser, q, "expected left parenthesis '('");

        sql_parse_advance(parser);
        sql_parse_select_field_list(parser, q);

        if (parser->current.type != TOKEN_RIGHT_PAREN)
            return sql_parse_error(parser, q, "expected right parenthesis ')'");
        sql_parse_advance(parser);

    default:
    }
}

static void sql_parse_select_from(struct sql_parser *parser,
                                  struct sql_query *q) {
    sql_parse_advance(parser);

    if (parser->previous.type != TOKEN_FROM)
        return sql_parse_error(parser, q, "expected 'FROM'");

    sql_parse_advance(parser);
    if (parser->previous.type != TOKEN_IDENTIFIER)
        return sql_parse_error(parser, q, "expected identifier");

    q->table_name = parser->previous.start;
    q->table_name_len = parser->previous.length;
}

static void sql_parse_select_condition(struct sql_parser *parser,
                                       struct sql_query *q) {

    const char *field_name, *field_value;
    size_t field_name_len, field_value_len;
    bool field_value_numeric;

    sql_parse_advance(parser);
    if (parser->current.type != TOKEN_IDENTIFIER)
        return sql_parse_error(parser, q, "expected field identifier");

    field_name = parser->current.start;
    field_name_len = parser->current.length;

    sql_parse_advance(parser);
    // only supporting = for now
    if (parser->current.type != TOKEN_EQUAL) {
        return sql_parse_error(parser, q, "expected '='");
    }

    sql_parse_advance(parser);
    if (parser->current.type != TOKEN_STRING &&
        parser->current.type != TOKEN_NUMBER)
        return sql_parse_error(parser, q, "expected a string or number");

    field_value = parser->current.start;
    field_value_len = parser->current.length;
    field_value_numeric = parser->current.type == TOKEN_NUMBER;

    q->conditions[q->conditions_count++] = (struct sql_select_condition){
        .field_name = field_name,
        .field_name_len = field_name_len,
        .field_value = field_value + 1,         // skip lead quote mark
        .field_value_len = field_value_len - 2, // skip ending quote mark
        .is_numeric = field_value_numeric,
    };

    sql_parse_advance(parser);
}

static void sql_parse_select_where(struct sql_parser *parser,
                                   struct sql_query *q) {

    while (1) {
        sql_parse_select_condition(parser, q);

        if (parser->current.type != TOKEN_AND) {
            break;
        }
    }

    if (parser->current.type != TOKEN_END)
        return sql_parse_error(parser, q, "expected statement end or 'and'");
}

static void sql_parse_select(struct sql_parser *parser, struct sql_query *q) {
    q->type = SQL_SELECT_STATEMENT;

    // parse field list
    sql_parse_select_field_list(parser, q);
    if (q->parse_error)
        return;

    // parse field list
    sql_parse_select_from(parser, q);
    if (q->parse_error)
        return;

    if (parser->current.type == TOKEN_WHERE)
        sql_parse_select_where(parser, q);
    else if (parser->current.type != TOKEN_END)
        return sql_parse_error(parser, q,
                               "expected statement end or where clause");
}

// CREATE TABLE orange (id integer primary key, pineapple text,strawberry
// text,banana text,grape text,apple text);
//
static void sql_parse_create_table_field(struct sql_parser *parser,
                                         struct sql_query *q) {

    if (parser->current.type != TOKEN_IDENTIFIER &&
        parser->current.type !=
            TOKEN_STRING) // sqlite allows double quoted strings in field names
        return sql_parse_error(parser, q, "expected identifier");
    sql_parse_advance(parser);

    q->fields[q->fields_count].field_name = parser->previous.start;
    q->fields[q->fields_count].field_len = parser->previous.length;
    if (parser->previous.type == TOKEN_STRING) {
        // strip quotes from quoted field names
        q->fields[q->fields_count].field_name++;
        q->fields[q->fields_count].field_len -= 2;
    }
    q->fields_count++;

    // consume column constrains tokens
    for (;;) {
        switch (parser->current.type) {
        case TOKEN_COMMA:
        case TOKEN_RIGHT_PAREN:
            goto done;
        case TOKEN_INTEGER:
        case TOKEN_TEXT:
        case TOKEN_KEY:
        case TOKEN_PRIMARY:
        case TOKEN_AUTOINCREMENT:
        case TOKEN_UNIQUE:
        case TOKEN_NOT:
        case TOKEN_NULL:
            break; // currently we don't do anything with these
        default:
            return sql_parse_error(parser, q, "unexpected token");
        }
        sql_parse_advance(parser);
    }

done:
}

static void sql_parse_create_table(struct sql_parser *parser,
                                   struct sql_query *q) {

    q->type = SQL_CREATE_TABLE_STATEMENT;

    // table name
    if (parser->current.type != TOKEN_IDENTIFIER &&
        parser->current.type != TOKEN_STRING)
        return sql_parse_error(parser, q, "expected identifier");
    sql_parse_advance(parser);

    q->table_name = parser->previous.start;
    q->table_name_len = parser->previous.length;

    if (parser->previous.type == TOKEN_STRING) {
        // strip quotes from quoted field names
        q->table_name++;
        q->table_name_len -= 2;
    }

    // field list openning
    if (parser->current.type != TOKEN_LEFT_PAREN)
        return sql_parse_error(parser, q, "expected '(");
    sql_parse_advance(parser);

    for (;;) {

        sql_parse_create_table_field(parser, q);

        if (q->parse_error)
            break;

        // keep going until closing
        if (parser->current.type == TOKEN_RIGHT_PAREN)
            break;

        if (parser->current.type != TOKEN_COMMA)
            return sql_parse_error(parser, q, "expected ',' or ')");

        sql_parse_advance(parser);
    }

    // consume closing parenthesis
    sql_parse_advance(parser);
}

static void sql_parse_create_index(struct sql_parser *parser,
                                   struct sql_query *q) {
    // unused for now
}

static void sql_parse_create(struct sql_parser *parser, struct sql_query *q) {

    sql_parse_advance(parser);
    switch (parser->previous.type) {
    case TOKEN_TABLE:
        return sql_parse_create_table(parser, q);
    case TOKEN_INDEX:
        return sql_parse_create_index(parser, q);
    default:
        return sql_parse_error(parser, q,
                               "expected keyword 'table' or 'index'");
    }
}

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

struct sql_query sql_parse(const char *query) {
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
        .conditions_count = 0,
    };

    sql_parse_advance(&parser);
    sql_parse_sql_stmt(&parser, &q);

    return q;
}
