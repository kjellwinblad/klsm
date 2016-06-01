#include <stdio.h>
#include "binheap.h"

int main () {
    heap_t *h = malloc(sizeof (heap_t));
    h-> len = 0;
    h->size = MAX_HEAP_SIZE;
    push(h, 3, 3);
    push(h, 4, 4);
    push(h, 5, 5);
    push(h, 1, 1);
    push(h, 2, 2);
    int i;
    for (i = 0; i < 5; i++) {
        unsigned long key, value;
        pop(h, &key, &value);
        printf("%lu %lu\n", key,value);
    }
    return 0;
}
