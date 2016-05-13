#include "seq_lock.h"

_Alignas(CACHE_LINE_SIZE)
OOLockMethodTable SEQ_LOCK_METHOD_TABLE = 
{
     .free = &free,
     .lock = &seq_lock,
     .unlock = &seq_unlock,
     .is_locked = &seq_is_locked,
     .try_lock = &seq_try_lock,
     .rlock = &seq_lock,
     .runlock = &seq_unlock,
     .delegate = &seq_delegate,
     .delegate_wait = &seq_delegate,
     .delegate_or_lock = &seq_delegate_or_lock,
     .close_delegate_buffer = NULL, /* Should never be called */
     .delegate_unlock = &seq_unlock
};



void seq_initialize(SEQLock * lock){
    atomic_init( &lock->counter.value, 2 );
}


void seq_lock(void * lock) {
    SEQLock *l = (SEQLock*)lock;
    unsigned long counter;
    unsigned long counterCopy;
    do{
        counter = atomic_load_explicit(&l->counter.value,
                                       memory_order_acquire);
        while((counter % 2) == 1){
            thread_yield();         
            counter = atomic_load_explicit(&l->counter.value,
                                           memory_order_acquire);
        }
        counterCopy = counter;
    }while(!atomic_compare_exchange_strong( &l->counter.value,
                                            &counterCopy, counter + 1 ));
}


void seq_delegate(void * lock,
                    void (*funPtr)(unsigned int, void *), 
                    unsigned int messageSize,
                    void * messageAddress){
    SEQLock *l = (SEQLock*)lock;
    seq_lock(l);
    funPtr(messageSize, messageAddress);
    seq_unlock(l);
}


void * seq_delegate_or_lock(void * lock, unsigned int messageSize){
    (void)messageSize;
    SEQLock *l = (SEQLock*)lock;
    seq_lock(l);
    return NULL;
}



SEQLock * plain_seq_create(){
    SEQLock * l = aligned_alloc(CACHE_LINE_SIZE, sizeof(SEQLock));
    seq_initialize(l);
    return l;
}


OOLock * oo_seq_create(){
    SEQLock * l = plain_seq_create();
    OOLock * ool = aligned_alloc(CACHE_LINE_SIZE, sizeof(OOLock));
    ool->lock = l;
    ool->m = &SEQ_LOCK_METHOD_TABLE;
    return ool;
}
