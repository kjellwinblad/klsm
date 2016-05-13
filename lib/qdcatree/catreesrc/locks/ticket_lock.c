#include "ticket_lock.h"

_Alignas(CACHE_LINE_SIZE)
OOLockMethodTable Ticket_LOCK_METHOD_TABLE = 
{
     .free = &free,
     .lock = &ticket_lock,
     .unlock = &ticket_unlock,
     .is_locked = &ticket_is_locked,
     .try_lock = &ticket_try_lock,
     .rlock = &ticket_lock,
     .runlock = &ticket_unlock,
     .delegate = &ticket_delegate,
     .delegate_wait = &ticket_delegate,
     .delegate_or_lock = &ticket_delegate_or_lock,
     .close_delegate_buffer = NULL, /* Should never be called */
     .delegate_unlock = &ticket_unlock
};



void ticket_initialize(TicketLock * lock){
    //    printf("TICKET LOCK inti %p\n", lock);
    atomic_init( &lock->ingress, 0 );
    atomic_init( &lock->egress, 0 );
}

void ticket_delegate(void * lock,
                    void (*funPtr)(unsigned int, void *), 
                    unsigned int messageSize,
                    void * messageAddress){
    TicketLock *l = (TicketLock*)lock;
    ticket_lock(l);
    funPtr(messageSize, messageAddress);
    ticket_unlock(l);
}


void * ticket_delegate_or_lock(void * lock, unsigned int messageSize){
    (void)messageSize;
    TicketLock *l = (TicketLock*)lock;
    ticket_lock(l);
    return NULL;
}



TicketLock * plain_ticket_create(){
    TicketLock * l = aligned_alloc(CACHE_LINE_SIZE, sizeof(TicketLock));
    ticket_initialize(l);
    return l;
}


OOLock * oo_ticket_create(){
    TicketLock * l = plain_ticket_create();
    OOLock * ool = aligned_alloc(CACHE_LINE_SIZE, sizeof(OOLock));
    ool->lock = l;
    ool->m = &Ticket_LOCK_METHOD_TABLE;
    return ool;
}
