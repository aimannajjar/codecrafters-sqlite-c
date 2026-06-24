#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>
#include <stdio.h>


struct btree_header {
    unsigned char page_type;
    uint16_t first_freeblock;
    uint16_t cells_count;
};

int btree_header_read(struct btree_header *header, FILE *stream);

#endif
