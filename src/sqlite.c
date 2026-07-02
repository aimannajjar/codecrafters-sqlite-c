#include "database.h"
#include "page.h"
#include <stdio.h>

int sqlite_cmd_dbinfo(struct db *db, FILE *database_file) {
    struct btree_header schema_page_header;

    if (read_schema_page_header(&schema_page_header, database_file)) {
        return -1;
    }

    printf("database page size: %u\n", (unsigned)db->page_size);
    printf("number of tables: %u\n", (unsigned)schema_page_header.cells_count);

    return 0;
}

int sqlite_cmd_tables(struct db *db, FILE *database_file) {
    struct schema_record *record;
    int rlen;
    if ((rlen = read_schema_table(&record, database_file)) < 0) {
        return -1;
    }

    for (int i = 0; i < rlen; i++) {
        printf("%s\t", record[i].tbl_name);
    }

    puts("");
    return 0;
}
