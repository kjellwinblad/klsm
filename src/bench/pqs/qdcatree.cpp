#include "qdcatree.h"

extern "C" {
#include "skiplist_qdcatree_set.h"
#include "linden/gc/gc.h"
}

namespace kpqbench
{

    
struct qdcatree_t {
    char pad1[64 - sizeof(SLCATreeSet *)]; 
    SLCATreeSet *pq;
    char pad2[64]; 
};

static inline void
qdcatree_insert(SLCATreeSet *pq,
              const uint32_t k,
              const uint32_t v)
{
    slqdcatree_put(pq,
                   (unsigned long) k,
                   (unsigned long) v);
}

QDCATree::QDCATree()
{
    _init_gc_subsystem();
    init_thread(1);
    m_q = new qdcatree_t;
    m_q->pq = slqdcatree_new();
}

QDCATree::~QDCATree()
{

}

void
QDCATree::init_thread(const size_t nthreads)
{
}
    
void
QDCATree::insert(const uint32_t &key,
                 const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

void
QDCATree::insert(const size_t &key,
                 const size_t &value)
{
    slqdcatree_put(m_q->pq,
                   (unsigned long) key,
                   (unsigned long) value);
}

bool
QDCATree::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)slqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back != ((unsigned long)-1);
}

bool
QDCATree::delete_min(size_t &k, size_t &v)
{
    unsigned long key_write_back;
    v = (size_t)slqdcatree_remove_min(m_q->pq, &key_write_back);
    k = (size_t)key_write_back;
    return key_write_back != ((unsigned long)-1);
}

}
