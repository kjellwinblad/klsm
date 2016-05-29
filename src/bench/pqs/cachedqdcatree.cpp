#include "cachedqdcatree.h"

extern "C" {
#include "skiplist_cachedqdcatree_set.h"
#include "linden/gc/gc.h"
}

namespace kpqbench
{

    
struct cachedqdcatree_t {
    char pad1[64 - sizeof(CSLCATreeSet *)]; 
    CSLCATreeSet *pq;
    char pad2[64]; 
};

static inline void
qdcatree_insert(CSLCATreeSet *pq,
              const uint32_t k,
              const uint32_t v)
{
    cslqdcatree_put(pq,
                   (unsigned long) k,
                   (unsigned long) v);
}

CachedQDCATree::CachedQDCATree()
{
    _init_gc_subsystem();
    init_thread(1);
    m_q = new cachedqdcatree_t;
    m_q->pq = cslqdcatree_new();
}

CachedQDCATree::~CachedQDCATree()
{

}

void
CachedQDCATree::init_thread(const size_t nthreads)
{
}
    
void
CachedQDCATree::insert(const uint32_t &key,
                       const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

void
CachedQDCATree::insert(const size_t &key,
                 const size_t &value)
{
    cslqdcatree_put(m_q->pq,
                   (unsigned long) key,
                   (unsigned long) value);
}

void
CachedQDCATree::flush_insert_cache()
{
    cslqdcatree_put_flush(m_q->pq);
}

bool
CachedQDCATree::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)cslqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back != ((unsigned long)-1);
}

bool
CachedQDCATree::delete_min(size_t &k, size_t &v)
{
    unsigned long key_write_back;
    v = (size_t)cslqdcatree_remove_min(m_q->pq, &key_write_back);
    k = (size_t)key_write_back;
    return key_write_back != ((unsigned long)-1);
}

}
