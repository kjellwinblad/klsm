#ifndef SLCATREE_SET_H
#define SLCATREE_SET_H

#include "ssalloc.h"
#define SLCATREE_MALLOC(size) ssalloc_alloc(1, size);
#define SLCATREE_FREE(data) ;

#ifndef SLCATREE_MALLOC
#      define SLCATREE_MALLOC(size) malloc(size)
#endif

#ifndef SLCATREE_FREE
#      define SLCATREE_FREE(data) free(data)
#endif

typedef struct sl_catree_set SLCATreeSet;

void slqdcatree_put(SLCATreeSet * set,
                    unsigned long key,
                    unsigned long value);
unsigned long slqdcatree_remove_min(SLCATreeSet * set, unsigned long * key_write_back);
void slqdcatree_delete(SLCATreeSet * setParam);
SLCATreeSet * slqdcatree_new();

#endif
