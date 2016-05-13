#include "hle_lock.h"
#include <immintrin.h> // For _mm_pause
_Alignas(CACHE_LINE_SIZE)
OOLockMethodTable HLE_LOCK_METHOD_TABLE = 
{
     .free = &free,
     .lock = &hle_lock,
     .unlock = &hle_unlock,
     .is_locked = &hle_is_locked,
     .try_lock = &hle_try_lock,
     .rlock = &hle_lock,
     .runlock = &hle_unlock,
     .delegate = &hle_delegate,
     .delegate_wait = &hle_delegate,
     .delegate_or_lock = &hle_delegate_or_lock,
     .close_delegate_buffer = NULL, /* Should never be called */
     .delegate_unlock = &hle_unlock
};



void hle_initialize(HLELock * lock){
    atomic_init( &lock->lockFlag.value, 0 );
}


void hle_delegate(void * lock,
                  void (*funPtr)(unsigned int, void *), 
                  unsigned int messageSize,
                  void * messageAddress){
    HLELock *l = (HLELock*)lock;
    hle_lock(l);
    funPtr(messageSize, messageAddress);
    hle_unlock(l);
}


void * hle_delegate_or_lock(void * lock, unsigned int messageSize){
    (void)messageSize;
    HLELock *l = (HLELock*)lock;
    hle_lock(l);
    return NULL;
}



HLELock * plain_hle_create(){
    HLELock * l = aligned_alloc(CACHE_LINE_SIZE, sizeof(HLELock));
    hle_initialize(l);
    return l;
}


OOLock * oo_hle_create(){
    HLELock * l = plain_hle_create();
    OOLock * ool = aligned_alloc(CACHE_LINE_SIZE, sizeof(OOLock));
    ool->lock = l;
    ool->m = &HLE_LOCK_METHOD_TABLE;
    return ool;
}
