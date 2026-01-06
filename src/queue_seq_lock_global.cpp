#include "queue_seq_lock_global.h"


#include <cstdlib>
#include <new>

// Define TLS freelist storage (exactly once)
thread_local ThreadLocalFreeList<QueueSeqLockGlobal::Node> QueueSeqLockGlobal::free_list;

QueueSeqLockGlobal::QueueSeqLockGlobal()
    : head(nullptr), tail(nullptr) {}

QueueSeqLockGlobal::Node* QueueSeqLockGlobal::get_node() {
    Node* n = free_list.pop();
    if (!n) {
        void* mem = std::malloc(sizeof(Node));
        if (!mem) std::abort();
        n = new (mem) Node();
        tls_stats.malloc_count++;
    } else {
        tls_stats.reused_count++;
    }
    n->next = nullptr;
    n->setNextFL(nullptr);
    return n;
}

void QueueSeqLockGlobal::free_node(Node* n) {
    n->next = nullptr;
    free_list.push(n); // updates freelist stats automatically
}

void QueueSeqLockGlobal::queue_init() {
    IQueue::queue_init();
    omp_init_lock(&q_lock);

    // Create initial sentinel (fresh allocation, not from freelist)
    void* mem = std::malloc(sizeof(Node));
    if (!mem) std::abort();
    head = new (mem) Node();
    head->next = nullptr;
    tail = head;
}

void QueueSeqLockGlobal::queue_destroy() {
    // Free remaining nodes still linked in the queue (including sentinel)
    Node* cur = head;
    while (cur) {
        Node* next = cur->next;
        cur->~Node();
        std::free(cur);
        cur = next;
    }
    head = tail = nullptr;

    omp_destroy_lock(&q_lock);
    // TLS freelist cleans itself up automatically when threads exit
}

void QueueSeqLockGlobal::enq(value_t v) {
    tls_stats.enq_count++;

    Node* n = get_node();
    n->v = v;
    n->next = nullptr;

    omp_set_lock(&q_lock);
        tail->next = n;
        tail = n;
    omp_unset_lock(&q_lock);
}

int QueueSeqLockGlobal::deq(value_t* v) {
    omp_set_lock(&q_lock);

    Node* first = head->next;
    if (!first) {
        tls_stats.failed_deq_count++;
        omp_unset_lock(&q_lock);
        return 0;
    }

    *v = first->v;
    tls_stats.deq_count++;

    Node* old_sentinel = head;
    head = first;

    omp_unset_lock(&q_lock);

    // Recycle old sentinel into the calling thread's freelist
    free_node(old_sentinel);
    return 1;
}
