//Exercise 4 - Concurrent queue with two locks and local freelist.

#include "two_lock_queue.h"

#include <cstdlib>
#include <new>


thread_local ThreadLocalFreeList<TwoLockQueue::Node> TwoLockQueue::free_list;


TwoLockQueue::TwoLockQueue()
    : head(nullptr), tail(nullptr) {}


TwoLockQueue::Node* TwoLockQueue::get_node() {
    Node* n = free_list.pop();
    if (!n) {
        void* mem = std::malloc(sizeof(Node));
        if (!mem) std::abort();
        n = new (mem) Node();
        tls_stats.malloc_count++;
    } else {
        // freelist.pop() already increments freelist_pops
        tls_stats.reused_count++;
    }

    n->next = nullptr;
    n->setNextFL(nullptr);
    return n;
}

void TwoLockQueue::free_node(Node* n) {
    n->next = nullptr;
    // freelist.push() updates freelist_pushes + freelist_max_size
    free_list.push(n);
}


void TwoLockQueue::queue_init() {
    IQueue::queue_init();

    omp_init_lock(&enqueue_lock);
    omp_init_lock(&dequeue_lock);

    // Create sentinel node
    void* mem = std::malloc(sizeof(Node));
    if (!mem) std::abort();
    head = new (mem) Node();
    head->next = nullptr;
    tail = head;
}


void TwoLockQueue::queue_destroy() {
    Node* cur = head;
    while (cur) {
        Node* next = cur->next;
        cur->~Node();
        std::free(cur);
        cur = next;
    }
    head = tail = nullptr;

    omp_destroy_lock(&enqueue_lock);
    omp_destroy_lock(&dequeue_lock);
}


void TwoLockQueue::enq(value_t v) {
    tls_stats.enq_count++;

    Node* n = get_node();
    n->v = v;
    n->next = nullptr;

    omp_set_lock(&enqueue_lock);
        tail->next = n;
        tail = n;
    omp_unset_lock(&enqueue_lock);
}

int TwoLockQueue::deq(value_t* v) {
    omp_set_lock(&dequeue_lock);

    Node* old_sentinel = head;
    Node* first = old_sentinel->next;

    if (!first) {
        tls_stats.failed_deq_count++;
        omp_unset_lock(&dequeue_lock);
        return 0;
    }

    *v = first->v;
    tls_stats.deq_count++;

    // Advance sentinel
    head = first;

    // Fix tail if queue became empty
    if (tail == old_sentinel) {
        omp_set_lock(&enqueue_lock);
        if (tail == old_sentinel) {
            tail = head;
        }
        omp_unset_lock(&enqueue_lock);
    }

    omp_unset_lock(&dequeue_lock);

    // Recycle old sentinel
    free_node(old_sentinel);
    return 1;
}
