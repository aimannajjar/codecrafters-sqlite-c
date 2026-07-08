#ifndef SQLITE_H
#define SQLITE_H

#include "database.h"
#include "sql.h"
#include <stdio.h>

#define SEARCH_MAX_RESULTS 4096

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file);
int sqlite_cmd_tables(struct db *db, FILE *database_file);
int sqlite_cmd_sql_stmt(char *stmt, struct db *db, FILE *database_file);
int sqlite_sql_stmt_exec(struct schema_record *schema_record,
                         struct sql_query *query, struct db *db,
                         FILE *database_file);

int sqlite_sql_stmt_exec_select_rowid(uint64_t rowid, struct schema_record *ddl,
                                      struct sql_query *query, struct db *db,
                                      FILE *database_file);

int sqlite_sql_search_index(struct db *db, struct schema_record *index_ddl,
                            struct schema_record *table_ddl,
                            struct sql_query *query, FILE *database_file);
#endif
