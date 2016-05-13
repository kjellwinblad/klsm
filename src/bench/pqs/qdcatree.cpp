#include "qdcatree.h"

extern "C" {
#include "skiplist_qdcatree_set.h"
}

namespace kpqbench
{

static __thread bool initializedqd= false;
    
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
    if (!initializedqd) {
        ssalloc_init();
        initializedqd = true;
    }
}
    
void
QDCATree::insert(const uint32_t &key,
                 const uint32_t &value)
{
    qdcatree_insert(m_q->pq, key, value);
}

bool
QDCATree::delete_min(uint32_t &v)
{
    unsigned long key_write_back;
    v = (uint32_t)slqdcatree_remove_min(m_q->pq, &key_write_back);
    return key_write_back == ((unsigned long)-1);
}

}
