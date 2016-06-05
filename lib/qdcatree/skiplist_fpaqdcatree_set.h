#ifndef FPASLCATREE_SET_H
#define FPASLCATREE_SET_H
#include <stdbool.h>
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
void fpaslqdcatree_put_param(FPASLCATreeSet * set,
                             unsigned long key,
                             unsigned long value,
                             bool catree_adapt);
unsigned long fpaslqdcatree_remove_min(FPASLCATreeSet * set, unsigned long * key_write_back);
unsigned long fpaslqdcatree_remove_min_param(FPASLCATreeSet * set,
                                             unsigned long * key_write_back,
                                             bool remove_min_relax,
                                             bool put_relax,
                                             bool catree_adapt);
void fpaslqdcatree_delete(FPASLCATreeSet * setParam);
FPASLCATreeSet * fpaslqdcatree_new();
void fpaslqdcatree_put_flush(FPASLCATreeSet * set);

#endif
