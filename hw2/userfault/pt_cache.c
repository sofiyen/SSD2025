#include "pt_cache.h"
#include <sys/mman.h>

#define PAGE_SIZE 4096

struct pt_cache_entry {
    unsigned long pfn;
    int level;
    void *user_va;
    struct pt_cache_entry *next;
};

static struct pt_cache_entry *cache_head = NULL;

void cache_insert(unsigned long pfn, int level, void *va) {
    struct pt_cache_entry *entry = malloc(sizeof(struct pt_cache_entry));
    entry->pfn = pfn;
    entry->level = level;
    entry->user_va = va;
    entry->next = cache_head;
    cache_head = entry;
}

void *cache_lookup(unsigned long pfn, int level) {
    for (struct pt_cache_entry *cur = cache_head; cur; cur = cur->next) {
        if (cur->pfn == pfn && cur->level == level)
            return cur->user_va;
    }
    return NULL;
}

void cache_cleanup(void) {
    struct pt_cache_entry *cur = cache_head;
    while (cur) {
        munmap(cur->user_va, PAGE_SIZE);
        struct pt_cache_entry *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    cache_head = NULL;
}
