#include "pt_cache.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define BIT_MASK ((1UL << 9) - 1) 

struct pt_cache_entry {
    unsigned long pfn;
    int level;
    void *user_va;
    unsigned long search_va;
    struct pt_cache_entry *next;
};

static struct pt_cache_entry *cache_head = NULL;

void cache_insert(unsigned long pfn, int level, void *va, unsigned long id_va) {
    if(level == 3){
        fprintf(stderr, "[cache] inserted PTE page table: va for index = 0x%lx\n", id_va);
    }
    struct pt_cache_entry *entry = malloc(sizeof(struct pt_cache_entry));
    if (!entry) {
        fprintf(stderr, "Failed to allocate memory for cache entry\n");
        return;
    }
    entry->pfn = pfn;
    entry->level = level;
    entry->user_va = va;
    entry->search_va = id_va;
    entry->next = cache_head;
    cache_head = entry;
}

void *cache_lookup(unsigned long pfn, int level) {
    for (struct pt_cache_entry *cur = cache_head; cur; cur = cur->next) {
        if (cur->pfn == pfn && cur->level == level) {
            fprintf(stderr, "Cache hit: pfn=0x%lx, level=%d, va=%p\n", pfn, level, cur->user_va);
            return cur->user_va;
        }
    }
    fprintf(stderr, "Cache miss: pfn=0x%lx, level=%d\n", pfn, level);
    return NULL;
}

void cache_cleanup(void) {
    unsigned long shifts[4] = {39, 30, 21, 12};
    int count = 0;
    struct pt_cache_entry *cur = cache_head;
    while (cur) {
        fprintf(stderr, "Freeing cache entry: pfn=0x%lx, level=%d, va=%p\n", 
               cur->pfn, cur->level, cur->user_va);
        
        if(cur->level == 3){
            unsigned long index = (cur->search_va >> shifts[cur->level]) & BIT_MASK;
            uint64_t *table = (uint64_t *)cur->user_va;
            table[index] = 0;
        }
        munmap(cur->user_va, PAGE_SIZE);
        struct pt_cache_entry *tmp = cur;
        cur = cur->next;
        free(tmp);
        count++;
    }
    fprintf(stderr, "Cleaned up %d cache entries\n", count);
    cache_head = NULL;
}