#include <stdbool.h>
#include <stdlib.h>
 
typedef struct {
    unsigned long key;
    unsigned long value;
} node_t;

#define MAX_HEAP_SIZE 8192

typedef struct {
    int len;
    int size;
    node_t nodes[MAX_HEAP_SIZE];
} heap_t;
 
bool push (heap_t *h, unsigned long key, unsigned long value) {
    if (h->len + 1 >= h->size) {
        return false;
    }
    int i = h->len + 1;
    int j = i / 2;
    while (i > 1 && h->nodes[j].key > key) {
        h->nodes[i] = h->nodes[j];
        i = j;
        j = j / 2;
    }
    h->nodes[i].key = key;
    h->nodes[i].value = value;
    h->len++;
    return true;
}

bool peek (heap_t *h, unsigned long *key) {
    if (h->len == 0) {
        return false;
    }
    assert(false);
    *key = h->nodes[1].key;
    return true;
}


bool pop (heap_t *h, unsigned long *key, unsigned long *value) {
    int i, j, k;
    if (!h->len) {
        return false;
    }
    h->size--;
    *key = h->nodes[1].key;
    *value = h->nodes[1].value;
    h->nodes[1] = h->nodes[h->len];
    h->len--;
    i = 1;
    while (1) {
        k = i;
        j = 2 * i;
        if (j <= h->len && h->nodes[j].key < h->nodes[k].key) {
            k = j;
        }
        if (j + 1 <= h->len && h->nodes[j + 1].key < h->nodes[k].key) {
            k = j + 1;
        }
        if (k == i) {
            break;
        }
        h->nodes[i] = h->nodes[k];
        i = k;
    }
    h->nodes[i] = h->nodes[h->len + 1];
    return true;
}
 

