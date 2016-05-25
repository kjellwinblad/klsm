#include "linden.h"

extern "C" {
#include "spraylist/gc/gc.h"
#include "spraylist/linden.h"
}

namespace kpqbench
{

struct linden_t {
    pq_t *pq;
};

static inline void
linden_insert(pq_t *pq,
              const uint32_t k,
              const uint32_t v)
{
    insert(pq, k, v);
}

Linden::Linden(const int max_offset)
{
    _init_gc_subsystem();
    m_q = new linden_t;
    m_q->pq = pq_init(max_offset);
}

Linden::~Linden()
{
    pq_destroy(m_q->pq);
    delete m_q;
    _destroy_gc_subsystem();
}

void
Linden::insert(const uint32_t &key,
               const uint32_t &value)
{
    linden_insert(m_q->pq, key, value);
}

bool
Linden::delete_min(uint32_t &v)
{
    unsigned long value;
    v = deletemin(m_q->pq);
    return true;
}

}
