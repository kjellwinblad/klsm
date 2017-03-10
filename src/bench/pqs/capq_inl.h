#include "capq.h"
#include <iostream>

extern "C" {
#include "skiplist_fpaqdcatree_set.h"
#include "spraylist/gc/gc.h"
}


    
struct fpaqdcatree_t {
    char pad1[64 - sizeof(FPASLCATreeSet *)]; 
    FPASLCATreeSet *pq;
    char pad2[64]; 
};

static inline void
qdcatree_insert(FPASLCATreeSet *pq,
              const uint32_t k,
              const uint32_t v)
{
    fpaslqdcatree_put(pq,
                     (unsigned long) k,
                     (unsigned long) v);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
CAPQ<remove_min_relax, put_relax, catree_adapt>::CAPQ()
{
    _init_gc_subsystem();
    init_thread(1);
    m_q = new fpaqdcatree_t;
    m_q->pq = fpaslqdcatree_new();
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
CAPQ<remove_min_relax, put_relax, catree_adapt>::~CAPQ()
{

}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::init_thread(const size_t nthreads)
{
    (void)nthreads;
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::insert(const uint32_t &key,
                                                               const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::insert(const size_t &key,
                                                               const size_t &value)
{
    fpaslqdcatree_put_param(m_q->pq,
                            (unsigned long) key,
                            (unsigned long) value,
                            catree_adapt);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::flush_insert_cache()
{
    fpaslqdcatree_put_flush(m_q->pq);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
bool
CAPQ<remove_min_relax, put_relax, catree_adapt>::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)fpaslqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back != ((unsigned long)-1);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
bool
CAPQ<remove_min_relax, put_relax, catree_adapt>::delete_min(size_t &k, size_t &v)
{
    unsigned long key_write_back;
    v = (size_t)fpaslqdcatree_remove_min_param(m_q->pq, &key_write_back, remove_min_relax, put_relax, catree_adapt);
    k = (size_t)key_write_back;
    return key_write_back != ((unsigned long)-1);
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::signal_no_waste()
{
}

template <bool remove_min_relax, bool put_relax, bool catree_adapt>
void
CAPQ<remove_min_relax, put_relax, catree_adapt>::signal_waste()
{
}

    
