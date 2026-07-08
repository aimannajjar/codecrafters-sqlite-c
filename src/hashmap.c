#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "hashmap.h"

size_t hash(char *key) {
    int h = 0;
    char *c = key;
    int i = 0;
    while (*c) {
        h += *c;
        if (i == 1)
            break;
        c++;
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

int64_t hget(struct hashmap *map, char *key) {
    size_t hkey;
    char *value;
    struct node *node;

    hkey = hash(key);
    value = NULL;
    node = map->buckets[hkey];
    while (node && strcmp(key, node->key)) {
        node = node->next;
    }

    return node ? node->data : -1;
}

int hput(struct hashmap *map, char *key, int64_t value) {
    size_t hkey;
    struct node *entry;
    struct node **node;

    // build entry
    entry = malloc(sizeof *entry);
    if (!entry) {
        perror("hashmap::malloc");
        return -1;
    }
    strncpy(entry->key, key, sizeof entry->key);
    entry->key[sizeof entry->key - 1] = '\0';
    entry->data = value;
    // strncpy(entry->data, value, sizeof entry->data);
    // entry->data[sizeof entry->data - 1] = '\0';
    // entry->next = NULL;

    // find a place for it
    hkey = hash(key);
    node = map->buckets + hkey;
    while (*node) {
        *node = (*node)->next;
    }
    *node = entry;
    return 0;
}

