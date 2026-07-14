#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "hashmap.h"

static size_t hash(size_t n, const char key[n]) {
    int h = 0;
    int i = 0;
    while (i < n) {
        h += key[i];
        if (i == 1)
            break;
        i++;
    }
    return h % HASH_BUCKETS_LEN;
}
struct hashmap *hcreate() {
    struct hashmap *map = calloc(1, sizeof(struct hashmap));
    return map;
}

void hfree(struct hashmap **map) {
    free(*map);
    *map = NULL;
}

int64_t hget(struct hashmap *map, size_t n, const char key[n]) {
    size_t hkey;
    char *value;
    struct node *node;

    hkey = hash(n, key);
    value = NULL;
    node = map->buckets[hkey];
    while (node && memcmp(key, node->key, n)) {
        node = node->next;
    }

    return node ? node->data : -1;
}

int hput(struct hashmap *map, size_t n, const char key[n], int64_t value) {
    size_t hkey;
    struct node *entry;
    struct node **node;

    // build entry
    entry = malloc(sizeof *entry);
    if (!entry) {
        perror("hashmap::malloc");
        return -1;
    }
    // todo: use references
    strncpy(entry->key, key, n);
    entry->key[sizeof entry->key - 1] = '\0';
    entry->data = value;

    // find a place for it
    hkey = hash(n, key);
    node = map->buckets + hkey;
    while (*node) {
        node = &(*node)->next;
    }
    *node = entry;
    return 0;
}

