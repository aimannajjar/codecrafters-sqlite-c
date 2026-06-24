#include "database.h"
#include "endian.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int db_header_read(struct db *db, FILE *stream) {

    if (fread(db->header_string, 1, DB_HEADER_STRING_LEN, stream) !=
        DB_HEADER_STRING_LEN) {
        return -1;
    }

    if (!fread_be16(&db->page_size, stream)) {
        return -1;
    }

    if (fseek(stream, DB_HEADER_SIZE - DB_HEADER_STRING_LEN - sizeof(uint16_t),
              SEEK_CUR) != 0) {
        perror("fseek");
        return -1;
    }

    return 0;
}
