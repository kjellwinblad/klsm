#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <stdbool.h>

//#include "include/ssalloc.h"
/* #define SKIPLIST_MALLOC(size) ssalloc_alloc(1, size); */
/* #define SKIPLIST_FREE(data) ; */

#ifndef SKIPLIST_MALLOC
#define SKIPLIST_MALLOC(size) malloc(size)
#endif

#ifndef SKIPLIST_FREE
#define SKIPLIST_FREE free
#endif



#define SKIPLIST_NUM_OF_LEVELS 21

typedef struct skiplist_node SkiplistNode;

typedef struct skiplist Skiplist;
void skiplist_put(Skiplist * skiplist, unsigned long key, unsigned long value);
unsigned long  skiplist_remove_min(Skiplist * skiplist, unsigned long * key_write_back);
Skiplist * new_skiplist();
void skiplist_delete(Skiplist * skiplist);
bool skiplist_is_empty(Skiplist * skiplist);
bool skiplist_more_than_two_keys(Skiplist * skiplist);
unsigned long skiplist_max_key(Skiplist * skiplist);
/* This function assumes that the input skiplist has at least two different keys */
unsigned long skiplist_split(Skiplist * skiplist,
                             Skiplist ** left_writeback,
                             Skiplist ** right_writeback);
Skiplist * skiplist_join(Skiplist * left_skiplist,
                         Skiplist * right_skiplist);

#endif
