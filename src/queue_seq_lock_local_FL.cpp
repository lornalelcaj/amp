#include "queue_seq_lock_local_FL.h"


#include <cstdlib>
#include <new>

// Define TLS freelist storage (exactly once)
thread_local ThreadLocalFreeList<QueueSequentialLockLocalFL::Node> QueueSequentialLockLocalFL::free_list;

QueueSequentialLockLocalFL::QueueSequentialLockLocalFL()
    : head(nullptr), tail(nullptr) {}

QueueSequentialLockLocalFL::Node* QueueSequentialLockLocalFL::get_node() {
    Node* n = free_list.pop();
    if (!n) {
        Node* mem = (Node*)std::malloc(sizeof(Node));
        if (!mem) std::abort();
        *mem = Node();
        n = mem;
        tls_stats.malloc_count++;
    } else {
        tls_stats.reused_count++;
    }
    n->next = nullptr;
    return n;
}

void QueueSequentialLockLocalFL::free_node(Node* n) {
    n->next = nullptr;
    free_list.push(n); // updates freelist stats automatically
}

void QueueSequentialLockLocalFL::queue_init() {
    IQueue::queue_init();
    omp_init_lock(&q_lock);

    // Create initial sentinel (fresh allocation, not from freelist)
    Node* mem = (Node*)std::malloc(sizeof(Node));
    if (!mem) std::abort();
    *mem = Node();
    head = tail = mem;
    head->next = nullptr;
}

void QueueSequentialLockLocalFL::queue_destroy() {
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

void QueueSequentialLockLocalFL::thread_prepare() {
    IQueue::thread_prepare();
    free_list.reset(); // free_list is reset since omp likes to reuse threads
}

void QueueSequentialLockLocalFL::enq(value_t v) {
    Node* n = get_node();
    n->v = v;
    n->next = nullptr;

    omp_set_lock(&q_lock);
        tail->next = n;
        tail = n;
    omp_unset_lock(&q_lock);
}

int QueueSequentialLockLocalFL::deq(value_t* v) {
    omp_set_lock(&q_lock);

    Node* first = head->next;
    if (!first) {
        omp_unset_lock(&q_lock);
        return 0;
    }

    *v = first->v;

    Node* old_sentinel = head;
    head = first;

    omp_unset_lock(&q_lock);

    // Recycle old sentinel into the calling thread's freelist
    free_node(old_sentinel);
    return 1;
}
