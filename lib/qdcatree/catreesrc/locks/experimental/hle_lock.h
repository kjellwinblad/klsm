#ifndef HLE_LOCK_H
#define HLE_LOCK_H

#include "locks/oo_lock_interface.h"
#include "misc/padded_types.h"

#include "misc/bsd_stdatomic.h"//Until c11 stdatomic.h is available
#include "misc/thread_includes.h"//Until c11 thread.h is available
#include <stdbool.h>
#include <immintrin.h> // For _mm_pause
//Make sure compiler does not optimize away memory access
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

typedef struct HLELockImpl {
    LLPaddedInt lockFlag;
} HLELock;

void hle_initialize(HLELock * lock);

static inline
void hle_lock(void * lock) {
    HLELock *l = (HLELock*)lock;
    bool first = true;
    while(true){
        while(atomic_load_explicit(&l->lockFlag.value, 
                                   memory_order_acquire)){
            thread_yield();
        }
        if(first){
#ifdef NOHLE
            if( !__atomic_exchange_n(&l->lockFlag.value.__val, 1, __ATOMIC_ACQUIRE)){
#else
            if( !__atomic_exchange_n(&l->lockFlag.value.__val, 1, __ATOMIC_ACQUIRE|__ATOMIC_HLE_ACQUIRE)){
#endif
                return;
            }else{
#ifndef NOHLE
                _mm_pause(); /* Abort failed transaction */
#endif
            }
        }else{
            if( !__atomic_exchange_n(&l->lockFlag.value.__val, 1, __ATOMIC_ACQUIRE)){
                return;
            }
        }
        first = false;
        thread_yield();
   }
}

static inline
void hle_unlock(void * lock) {
    HLELock *l = (HLELock*)lock;
     /* Free lock with lock elision */
#ifdef NOHLE
     __atomic_store_n(&l->lockFlag.value.__val, 0, __ATOMIC_RELEASE);
#else
     __atomic_store_n(&l->lockFlag.value.__val, 0, __ATOMIC_RELEASE|__ATOMIC_HLE_RELEASE);
#endif
}
static inline
bool hle_is_locked(void * lock){
    HLELock *l = (HLELock*)lock;
    return atomic_load(&l->lockFlag.value);
}
static inline
bool hle_try_lock(void * lock) {
    HLELock *l = (HLELock*)lock;
    if(!atomic_load_explicit(&l->lockFlag.value, memory_order_acquire)){
#ifdef NOHLE
        if(__atomic_exchange_n(&l->lockFlag.value.__val, 1, __ATOMIC_ACQUIRE)){
#else
        if(__atomic_exchange_n(&l->lockFlag.value.__val, 1, __ATOMIC_ACQUIRE|__ATOMIC_HLE_ACQUIRE)){
            _mm_pause(); /* Abort failed transaction */
#endif
            return false;
        }else{
            return true;
        }
    } else {
        return false;
    }
}

void hle_delegate(void * lock,
                    void (*funPtr)(unsigned int, void *), 
                    unsigned int messageSize,
                    void * messageAddress);
void * hle_delegate_or_lock(void * lock, unsigned int messageSize);
HLELock * plain_hle_create();
OOLock * oo_hle_create();

#endif
