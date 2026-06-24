#include "page.h"
#include "endian.h"
#include <stdio.h>

int btree_header_read(struct btree_header *header, FILE *stream) { 
    if (fread(&header->page_type, 1, 1, stream) != 1) {
        return -1;
    }

    if (!fread_be16(&header->first_freeblock, stream)) {
        return -1;
    }

    if (!fread_be16(&header->cells_count, stream)) {
        return -1;
    }

    return 0;
}
