#ifndef SEQ_LOCK_H
#define SEQ_LOCK_H

#include "locks/oo_lock_interface.h"
#include "misc/padded_types.h"

#include "misc/bsd_stdatomic.h"//Until c11 stdatomic.h is available
#include "misc/thread_includes.h"//Until c11 thread.h is available
#include <stdbool.h>


typedef struct SEQLockImpl {
    LLPaddedULong counter;
} SEQLock;


void seq_initialize(SEQLock * lock);
void seq_lock(void * lock);

static inline
void seq_unlock(void * lock) {
    SEQLock *l = (SEQLock*)lock;
    atomic_fetch_add( &l->counter.value, 1 );
}

static inline
bool seq_is_locked(void * lock){
    SEQLock *l = (SEQLock*)lock;
    return (atomic_load(&l->counter.value) % 2) == 1;
}

static inline
unsigned long seq_start_read(void * lock){
    SEQLock *l = (SEQLock*)lock;
    unsigned long counter = atomic_load(&l->counter.value);
    if((counter % 2) == 0){
        return counter;
    }else{
        return 0;//Means invalid seq number, try again or force lock
    }
}

static inline
bool seq_validate_read(void * lock, unsigned long counterValue){
    SEQLock *l = (SEQLock*)lock;
    unsigned long counter = atomic_load(&l->counter.value);
    return counterValue == counter;
}

static inline
bool seq_try_lock(void * lock) {
    SEQLock *l = (SEQLock*)lock;
    unsigned long counter = atomic_load_explicit(&l->counter.value,
                                                 memory_order_acquire);
    if((counter % 2) == 0){
        return atomic_compare_exchange_strong( &l->counter.value,
                                               &counter, counter + 1 );
    }else{
        return false;
    }
}
void seq_delegate(void * lock,
                  void (*funPtr)(unsigned int, void *), 
                  unsigned int messageSize,
                  void * messageAddress);
void * seq_delegate_or_lock(void * lock, unsigned int messageSize);
SEQLock * plain_seq_create();
OOLock * oo_seq_create();

#endif
