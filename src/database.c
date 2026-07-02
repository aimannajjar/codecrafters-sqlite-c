#include "database.h"
#include "endian.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum CHARSET_ENC {
    UTF8 = 1,
    UTF_16LE,
    UTF_16BE,
};

uint16_t db_page_size = 0;
enum CHARSET_ENC db_text_encoding = -1;


/** reads the initial bytes of sqlite files before the first page 
 ** these include the magic string, the page_size constant and 
 ** text encoding among other fields
 **/
int db_header_read(struct db *db, FILE *stream) {

    if (fread(db->header_string, 1, DB_HEADER_STRING_LEN, stream) !=
        DB_HEADER_STRING_LEN) {
        return -1;
    }

    if (!fread_be16(&db->page_size, stream)) {
        return -1;
    }
    db_page_size = db->page_size; // global since it's needed in many places

    // parse encoding at byte 56 then go back 
    int r = ftell(stream);
    fseek(stream, 56, SEEK_SET);
    if (!fread_be32(&db_text_encoding, stream)) {
        puts("couldn't parse charset encoding");
        return -1;
    }
    fseek(stream, r, SEEK_SET);

    // skip remaining header fields for now until we really need them
    if (fseek(stream, DB_HEADER_SIZE - DB_HEADER_STRING_LEN - sizeof(uint16_t),
              SEEK_CUR) != 0) {
        perror("fseek");
        return -1;
    }

    return 0;
}
