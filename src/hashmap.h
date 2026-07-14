#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#define HASH_BUCKETS_LEN 100

struct node {
    char key[256];
    int64_t data;
    struct node *next;
};

struct hashmap {
    struct node *buckets[HASH_BUCKETS_LEN];
};

struct hashmap *hcreate();

void hfree(struct hashmap **map);
int64_t hget(struct hashmap *map, size_t n, const char key[n]);
int hput(struct hashmap *map, size_t n, const char key[n], int64_t value);

#endif // HASHMAP_H
