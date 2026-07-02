#ifndef SQLITE_H
#define SQLITE_H

#include "database.h"
#include <stdio.h>

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file);
int sqlite_cmd_tables(struct db *db, FILE *database_file);

#endif
