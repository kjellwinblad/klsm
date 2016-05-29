#ifndef __CACHEDQDCATREE_H
#define __CACHEDQDCATREE_H

#include <cstddef>
#include <cstdint>

namespace kpqbench
{

struct cachedqdcatree_t;

class CachedQDCATree
{
public:
    CachedQDCATree();
    virtual ~CachedQDCATree();

    void insert(const uint32_t &key, const uint32_t &value);
    void insert(const size_t &key, const size_t &value);
    void flush_insert_cache();
    bool delete_min(uint32_t &v);
    bool delete_min(size_t &k, size_t &v);

    void signal_waste(){}
    void signal_no_waste(){}
    
    void init_thread(const size_t nthreads);
    constexpr static bool supports_concurrency() { return true; }

private:
    cachedqdcatree_t *m_q;
};

}

#endif /* __CACHEDQDCATREE_H */
