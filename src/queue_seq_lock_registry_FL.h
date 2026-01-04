#pragma once

#include <cstdlib>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <omp.h>

#include "IQueue.h"
#include "thread_local_free_list.h"
#include "thread_stats_tls.h"

class QueueSequentialLockRegistryFL : public IQueue {
private:
    struct node_t {
        value_t v;
        node_t* next;     // queue linkage
        node_t* next_fl;  // freelist linkage

        node_t() : v(0), next(nullptr), next_fl(nullptr) {}

        inline node_t* getNextFL() { return next_fl; }
        inline void setNextFL(node_t* n) { next_fl = n; }
    };

    node_t* head;
    node_t* tail;
    omp_lock_t q_lock;

    // Registry to free all freelists at queue_destroy()
    omp_lock_t registry_lock;
    std::vector<ThreadLocalFreeList<node_t>*> registry;

    // TLS: per-thread freelist per queue instance
    static thread_local std::unordered_map<
        QueueSequentialLockRegistryFL*,
        ThreadLocalFreeList<node_t>*
    > tls_fls;

private:
    inline ThreadLocalFreeList<node_t>* get_fl() {
        auto it = tls_fls.find(this);
        return (it == tls_fls.end()) ? nullptr : it->second;
    }

    inline node_t* alloc_node() {
        ThreadLocalFreeList<node_t>* fl = get_fl();
        node_t* n = fl ? fl->pop() : nullptr;

        if (!n) {
            n = (node_t*)std::malloc(sizeof(node_t));
            if (!n) { std::perror("malloc"); std::abort(); }
            // Since node_t is trivial, just initialize fields directly
            n->v = 0;
            n->next = nullptr;
            n->next_fl = nullptr;
            tls_stats.malloc_count++;
        } else {
            tls_stats.reused_count++;
            // Defensive clearing of fields
            n->next = nullptr;
            n->next_fl = nullptr;
        }
        return n;
    }

    inline void free_node(node_t* n) {
        ThreadLocalFreeList<node_t>* fl = get_fl();
        if (!fl) {
            // If thread_prepare wasn't called, fall back safely.
            std::free(n);
            return;
        }
        n->next = nullptr;
        n->next_fl = nullptr;
        fl->push(n);
    }

public:
    QueueSequentialLockRegistryFL();
    ~QueueSequentialLockRegistryFL() override;

    void queue_init() override;
    void queue_destroy() override;

    void thread_prepare() override;
    void thread_cleanup() override;

    void enq(value_t v) override;
    int  deq(value_t* v) override;
};
