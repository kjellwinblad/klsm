#include "adaptivecachedqdcatree.h"

extern "C" {
#include "skiplist_adaptivecachedqdcatree_set.h"
#include "spraylist/gc/gc.h"
}

namespace kpqbench
{

    
struct adaptivecachedqdcatree_t {
    char pad1[64 - sizeof(ACSLCATreeSet *)]; 
    ACSLCATreeSet *pq;
    char pad2[64]; 
};

static inline void
qdcatree_insert(ACSLCATreeSet *pq,
              const uint32_t k,
              const uint32_t v)
{
    acslqdcatree_put(pq,
                     (unsigned long) k,
                     (unsigned long) v);
}

AdaptiveCachedQDCATree::AdaptiveCachedQDCATree()
{
    _init_gc_subsystem();
    init_thread(1);
    m_q = new adaptivecachedqdcatree_t;
    m_q->pq = acslqdcatree_new();
}

AdaptiveCachedQDCATree::~AdaptiveCachedQDCATree()
{

}

void
AdaptiveCachedQDCATree::init_thread(const size_t nthreads)
{
}
    
void
AdaptiveCachedQDCATree::insert(const uint32_t &key,
                       const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

void
AdaptiveCachedQDCATree::insert(const size_t &key,
                 const size_t &value)
{
    acslqdcatree_put(m_q->pq,
                   (unsigned long) key,
                   (unsigned long) value);
}

void
AdaptiveCachedQDCATree::flush_insert_cache()
{
    acslqdcatree_put_flush(m_q->pq);
}

bool
AdaptiveCachedQDCATree::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)acslqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back != ((unsigned long)-1);
}

bool
AdaptiveCachedQDCATree::delete_min(size_t &k, size_t &v)
{
    unsigned long key_write_back;
    v = (size_t)acslqdcatree_remove_min(m_q->pq, &key_write_back);
    k = (size_t)key_write_back;
    return key_write_back != ((unsigned long)-1);
}
   
void
AdaptiveCachedQDCATree::signal_no_waste()
{
    acslqdcatree_signal_no_waste(m_q->pq);
}

void
AdaptiveCachedQDCATree::signal_waste()
{
    acslqdcatree_signal_waste(m_q->pq);
}

    
}
