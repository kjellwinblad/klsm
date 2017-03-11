#ifndef THREAD_INCLUDES_H
#define THREAD_INCLUDES_H

#include <time.h>
#include <stdlib.h>
#include <pthread.h>//Until c11 threads.h is available
#include <time.h>
#include <sched.h>

static inline void thread_yield(){
    //sched_yield();
    //atomic_thread_fence(memory_order_seq_cst);
    asm("pause");
}

#ifndef __clang__
#    define _Thread_local __thread
#endif

#endif
