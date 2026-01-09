//Exercise 4 - Concurrent queue with two locks and local freelist.

#include "queue_split_lock_local_FL.h"

#include <cstdlib>


thread_local ThreadLocalFreeList<QueueSplitLockLocalFL::Node> QueueSplitLockLocalFL::free_list;


QueueSplitLockLocalFL::QueueSplitLockLocalFL()
    : head(NULL), tail(NULL) {}


QueueSplitLockLocalFL::Node* QueueSplitLockLocalFL::get_node() {
    Node* n = free_list.pop();
    if (!n) {
        Node* mem = (Node*)std::malloc(sizeof(Node));
        if (!mem) std::abort();
        *mem = Node();
        n = mem;
        tls_stats.malloc_count++;
    } else {
        // freelist.pop() already increments freelist_pops
        tls_stats.reused_count++;
    }

    n->next = NULL;
    return n;
}

void QueueSplitLockLocalFL::free_node(Node* n) {
    n->next = NULL;
    // freelist.push() updates freelist_pushes + freelist_max_size
    free_list.push(n);
}


void QueueSplitLockLocalFL::queue_init() {
    IQueue::queue_init();

    omp_init_lock(&enqueue_lock);
    omp_init_lock(&dequeue_lock);

    // Create sentinel node
    Node* mem = (Node*)std::malloc(sizeof(Node));
    if (!mem) std::abort();
    *mem = Node();
    head = mem;
    head->next = NULL;
    tail = head;
}


void QueueSplitLockLocalFL::queue_destroy() {
    Node* cur = head;
    while (cur) {
        Node* next = cur->next;
        std::free(cur);
        cur = next;
    }
    head = tail = NULL;

    omp_destroy_lock(&enqueue_lock);
    omp_destroy_lock(&dequeue_lock);
}

void QueueSplitLockLocalFL::thread_prepare() {
    IQueue::thread_prepare();
    free_list.reset(); // free_list is reset since omp likes to reuse threads
}

void QueueSplitLockLocalFL::enq(value_t v) {
    Node* n = get_node();
    n->v = v;
    n->next = NULL;

    omp_set_lock(&enqueue_lock);
        tail->next = n;
        tail = n;
    omp_unset_lock(&enqueue_lock);
}

int QueueSplitLockLocalFL::deq(value_t* v) {
    omp_set_lock(&dequeue_lock);

    Node* old_sentinel = head;
    Node* first = old_sentinel->next;

    if (!first) {
        omp_unset_lock(&dequeue_lock);
        return 0;
    }

    *v = first->v;

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
