#ifndef __ADAPTIVECACHEDQDCATREE_H
#define __ADAPTIVECACHEDQDCATREE_H

#include <cstddef>
#include <cstdint>

namespace kpqbench
{

struct adaptivecachedqdcatree_t;

class AdaptiveCachedQDCATree
{
public:
    AdaptiveCachedQDCATree();
    virtual ~AdaptiveCachedQDCATree();

    void insert(const uint32_t &key, const uint32_t &value);
    void insert(const size_t &key, const size_t &value);
    void flush_insert_cache();
    bool delete_min(uint32_t &v);
    bool delete_min(size_t &k, size_t &v);

    void signal_waste();
    void signal_no_waste();

    void init_thread(const size_t nthreads);
    constexpr static bool supports_concurrency() { return true; }

private:
    adaptivecachedqdcatree_t *m_q;
};

}

#endif /* __ADAPTIVECACHEDQDCATREE_H */
