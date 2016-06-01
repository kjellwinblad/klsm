#include "fpaqdcatree.h"
#include <iostream>

extern "C" {
#include "skiplist_fpaqdcatree_set.h"
#include "linden/gc/gc.h"
}

namespace kpqbench
{

    
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

FPAQDCATree::FPAQDCATree()
{
    _init_gc_subsystem();
    init_thread(1);
    m_q = new fpaqdcatree_t;
    m_q->pq = fpaslqdcatree_new();
}

FPAQDCATree::~FPAQDCATree()
{

}

void
FPAQDCATree::init_thread(const size_t nthreads)
{
}
    
void
FPAQDCATree::insert(const uint32_t &key,
                       const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

void
FPAQDCATree::insert(const size_t &key,
                 const size_t &value)
{
    fpaslqdcatree_put(m_q->pq,
                      (unsigned long) key,
                      (unsigned long) value);
}

void
FPAQDCATree::flush_insert_cache()
{
    fpaslqdcatree_put_flush(m_q->pq);
}

bool
FPAQDCATree::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)fpaslqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back != ((unsigned long)-1);
}

bool
FPAQDCATree::delete_min(size_t &k, size_t &v)
{
    unsigned long key_write_back;
    v = (size_t)fpaslqdcatree_remove_min(m_q->pq, &key_write_back);
    k = (size_t)key_write_back;
    return key_write_back != ((unsigned long)-1);
}
   
void
FPAQDCATree::signal_no_waste()
{
}

void
FPAQDCATree::signal_waste()
{
}

    
}
