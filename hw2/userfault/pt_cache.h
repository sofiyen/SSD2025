#ifndef PT_CACHE_H
#define PT_CACHE_H

#include <stdlib.h>

void cache_insert(unsigned long pfn, int level, void *va);
void *cache_lookup(unsigned long pfn, int level);
void cache_cleanup(void);

#endif
