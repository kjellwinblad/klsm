#ifndef CSLCATREE_SET_H
#define CSLCATREE_SET_H


#define CSLCATREE_MALLOC(size) malloc(size);
#define CSLCATREE_FREE(data) free

#ifndef CSLCATREE_MALLOC
#      define CSLCATREE_MALLOC(size) malloc(size)
#endif

#ifndef CSLCATREE_FREE
#      define CSLCATREE_FREE(data) free(data)
#endif

typedef struct csl_catree_set CSLCATreeSet;

void cslqdcatree_put(CSLCATreeSet * set,
                    unsigned long key,
                    unsigned long value);
unsigned long cslqdcatree_remove_min(CSLCATreeSet * set, unsigned long * key_write_back);
void cslqdcatree_delete(CSLCATreeSet * setParam);
CSLCATreeSet * cslqdcatree_new();
void cslqdcatree_put_flush(CSLCATreeSet * set);

#endif
