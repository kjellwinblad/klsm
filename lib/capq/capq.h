#ifndef CAPQ_H
#define CAPQ_H
#include <stdbool.h>

#ifndef SLCATREE_MALLOC
#      define SLCATREE_MALLOC(size) malloc(size)
#endif

#ifndef SLCATREE_FREE
#      define SLCATREE_FREE(data) free(data)
#endif

typedef struct fpasl_catree_set CAPQ;

void capq_put(CAPQ * set,
              unsigned long key,
              unsigned long value);
void capq_put_param(CAPQ * set,
                    unsigned long key,
                    unsigned long value,
                    bool catree_adapt);
unsigned long capq_remove_min(CAPQ * set, unsigned long * key_write_back);
unsigned long capq_remove_min_param(CAPQ * set,
                                    unsigned long * key_write_back,
                                    bool remove_min_relax,
                                    bool put_relax,
                                    bool catree_adapt);
void capq_delete(CAPQ * setParam);
CAPQ * capq_new();

#endif
