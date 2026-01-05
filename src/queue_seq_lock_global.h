#pragma once

#include <omp.h>
#include "IQueue.h"
#include "thread_local_free_list.h"
#include "thread_stats_tls.h"

// Sequential queue made concurrent using ONE global lock + per-thread freelist
class QueueSeqLockGlobal : public IQueue {
private:
    struct Node {
        value_t v;
        Node* next;     // queue linkage
        Node* nextFL;   // freelist linkage

        Node() : v(0), next(nullptr), nextFL(nullptr) {}

        // required by ThreadLocalFreeList
        Node* getNextFL() { return nextFL; }
        void  setNextFL(Node* n) { nextFL = n; }
    };

    Node* head;   // sentinel
    Node* tail;

    omp_lock_t q_lock; // global lock protecting ALL queue ops

    static thread_local ThreadLocalFreeList<Node> free_list;

    Node* get_node();
    void  free_node(Node* n);

public:
    QueueSeqLockGlobal();
    ~QueueSeqLockGlobal() override = default;

    void queue_init() override;
    void queue_destroy() override;

    void enq(value_t v) override;
    int  deq(value_t* v) override;
};
