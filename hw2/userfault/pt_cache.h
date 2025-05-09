#ifndef PT_CACHE_H
#define PT_CACHE_H

#include <stdlib.h>

void cache_insert(unsigned long pfn, int level, void *va, unsigned long id_va, int t);
void *cache_lookup(unsigned long pfn, int level, unsigned long va, int t);
void cache_cleanup(void);

#endif
