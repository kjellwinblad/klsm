#ifndef __QDCATREE_H
#define __QDCATREE_H

#include <cstddef>
#include <cstdint>

namespace kpqbench
{

struct qdcatree_t;

class QDCATree
{
public:
    QDCATree();
    virtual ~QDCATree();

    void insert(const uint32_t &key, const uint32_t &value);
    bool delete_min(uint32_t &v);

    void init_thread(const size_t nthreads);
    constexpr static bool supports_concurrency() { return true; }

private:
    qdcatree_t *m_q;
};

}

#endif /* __QDCATREE_H */
