#ifndef SQLITE_H
#define SQLITE_H

#include "database.h"
#include "sql.h"
#include <stdio.h>

#define SELECT_MAX_FIELD_COUNT 20

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file);
int sqlite_cmd_tables(struct db *db, FILE *database_file);
int sqlite_cmd_sql_stmt(char *stmt, struct db *db, FILE *database_file);
int sqlite_sql_stmt_exec(struct schema_record *schema_record,
                         struct sql_query *query, struct db *db,
                         FILE *database_file);

#endif
