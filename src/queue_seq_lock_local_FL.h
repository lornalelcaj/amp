#pragma once

#include <omp.h>
#include "IQueue.h"
#include "thread_local_free_list.h"
#include "thread_stats_tls.h"

// Sequential queue made concurrent using ONE global lock + per-thread freelist
class QueueSequentialLockLocalFL : public IQueue {
private:
    struct Node {
        value_t v;
        Node* next;     // queue linkage

        Node() : v(0), next(nullptr) {}

        // required by ThreadLocalFreeList
        Node* getNextFL() { return next; }
        void  setNextFL(Node* n) { next = n; }
    };

    Node* head;   // sentinel
    Node* tail;

    omp_lock_t q_lock; // global lock protecting ALL queue ops

    static thread_local ThreadLocalFreeList<Node> free_list;

    Node* get_node();
    void  free_node(Node* n);

public:
    QueueSequentialLockLocalFL();
    ~QueueSequentialLockLocalFL() override = default;

    void queue_init() override;
    void queue_destroy() override;

    void thread_prepare() override;

    void enq(value_t v) override;
    int  deq(value_t* v) override;
};
