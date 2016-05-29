#ifndef AACSLCATREE_SET_H
#define AACSLCATREE_SET_H


#define ACSLCATREE_MALLOC(size) malloc(size);
#define ACSLCATREE_FREE(data) free

#ifndef ACSLCATREE_MALLOC
#      define ACSLCATREE_MALLOC(size) malloc(size)
#endif

#ifndef ACSLCATREE_FREE
#      define ACSLCATREE_FREE(data) free(data)
#endif

typedef struct acsl_catree_set ACSLCATreeSet;

void acslqdcatree_put(ACSLCATreeSet * set,
                      unsigned long key,
                      unsigned long value);
unsigned long acslqdcatree_remove_min(ACSLCATreeSet * set, unsigned long * key_write_back);
void acslqdcatree_delete(ACSLCATreeSet * setParam);
ACSLCATreeSet * acslqdcatree_new();
void acslqdcatree_put_flush(ACSLCATreeSet * set);
void acslqdcatree_signal_no_waste(ACSLCATreeSet * set);
void acslqdcatree_signal_waste(ACSLCATreeSet * set);

#endif
