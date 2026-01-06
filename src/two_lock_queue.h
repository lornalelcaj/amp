#pragma once

#include <omp.h>
#include "IQueue.h"
#include "thread_local_free_list.h"

// Exercise 4: Concurrent queue with split locks (enqueue lock + dequeue lock)
class TwoLockQueue : public IQueue {
private:
    struct Node {
        value_t v;
        Node* next;     // queue linkage
        Node* nextFL;   // freelist linkage (required by ThreadLocalFreeList)

        Node() : v(0), next(nullptr), nextFL(nullptr) {}

        // Required by ThreadLocalFreeList
        Node* getNextFL() { return nextFL; }
        void  setNextFL(Node* n) { nextFL = n; }
    };

    Node* head; // points to current sentinel
    Node* tail; // last real node or sentinel if empty

    omp_lock_t enqueue_lock;
    omp_lock_t dequeue_lock;

    // TLS freelist for this queue's node type (defined in .cpp)
    static thread_local ThreadLocalFreeList<Node> free_list;

    Node* get_node();
    void  free_node(Node* n);

public:
    TwoLockQueue();
    ~TwoLockQueue() override = default;

    void queue_init() override;
    void queue_destroy() override;

    void enq(value_t v) override;
    int  deq(value_t* v) override;
};
