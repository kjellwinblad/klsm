#ifndef FPASLCATREE_SET_H
#define FPASLCATREE_SET_H

/* #include "include/ssalloc.h" */
/* #define SLCATREE_MALLOC(size) ssalloc_alloc(1, size); */
/* #define SLCATREE_FREE(data) ; */

#ifndef SLCATREE_MALLOC
#      define SLCATREE_MALLOC(size) malloc(size)
#endif

#ifndef SLCATREE_FREE
#      define SLCATREE_FREE(data) free(data)
#endif

typedef struct fpasl_catree_set FPASLCATreeSet;

void fpaslqdcatree_put(FPASLCATreeSet * set,
                    unsigned long key,
                    unsigned long value);
unsigned long fpaslqdcatree_remove_min(FPASLCATreeSet * set, unsigned long * key_write_back);
void fpaslqdcatree_delete(FPASLCATreeSet * setParam);
FPASLCATreeSet * fpaslqdcatree_new();
void fpaslqdcatree_put_flush(FPASLCATreeSet * set);

#endif
