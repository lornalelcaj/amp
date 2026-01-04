#include "queue_seq_lock_registry_FL.h"

thread_local std::unordered_map<
    QueueSequentialLockRegistryFL*,
    ThreadLocalFreeList<QueueSequentialLockRegistryFL::node_t>*
> QueueSequentialLockRegistryFL::tls_fls;

QueueSequentialLockRegistryFL::QueueSequentialLockRegistryFL()
    : head(nullptr), tail(nullptr) {}

QueueSequentialLockRegistryFL::~QueueSequentialLockRegistryFL() {
    // Defensive cleanup if  queue_destroy() was not called
    if (head != nullptr) {
        queue_destroy();
    }
}

void QueueSequentialLockRegistryFL::queue_init() {
    IQueue::queue_init();

    omp_init_lock(&q_lock);
    omp_init_lock(&registry_lock);
    registry.clear();

    node_t* sent = (node_t*)std::malloc(sizeof(node_t));
    if (!sent) { std::perror("malloc"); std::abort(); }
    sent->v = 0;
    sent->next = nullptr;
    sent->next_fl = nullptr;

    head = tail = sent;
}

void QueueSequentialLockRegistryFL::queue_destroy() {
    // Free remaining main-queue nodes 
    node_t* cur = head;
    while (cur) {
        node_t* tmp = cur->next;
        std::free(cur);
        cur = tmp;
    }
    head = tail = nullptr;

    // Move registry out under lock, then delete outside the lock
    std::vector<ThreadLocalFreeList<node_t>*> to_delete;
    omp_set_lock(&registry_lock);
    to_delete.swap(registry);
    omp_unset_lock(&registry_lock);

    for (auto* fl : to_delete) {
        delete fl;
    }

    omp_destroy_lock(&registry_lock);
    omp_destroy_lock(&q_lock);
}



void QueueSequentialLockRegistryFL::thread_prepare() {
    IQueue::thread_prepare(); // resets tls_stats

    // If this thread already has a freelist for this queue instance, reuse it
    if (tls_fls.find(this) != tls_fls.end()) return;

    auto* fl = new ThreadLocalFreeList<node_t>();

    // Register so queue_destroy() can delete it later
    omp_set_lock(&registry_lock);
    registry.push_back(fl);
    omp_unset_lock(&registry_lock);

    tls_fls[this] = fl;
}

void QueueSequentialLockRegistryFL::thread_cleanup() {
    IQueue::thread_cleanup();

    // Remove this queue's entry from this threads TLS map (prevents stale pointer)
    auto it = tls_fls.find(this);
    if (it != tls_fls.end()) {
        tls_fls.erase(it);
    }

}

void QueueSequentialLockRegistryFL::enq(value_t v) {
    node_t* n = alloc_node();
    n->v = v;
    n->next = nullptr;

    omp_set_lock(&q_lock);
    tail->next = n;
    tail = n;
    omp_unset_lock(&q_lock);

    tls_stats.enq_count++;
}

int QueueSequentialLockRegistryFL::deq(value_t* v) {
    node_t* old_sent = nullptr;
    int ok = 0;

    omp_set_lock(&q_lock);

    old_sent = head;
    node_t* first = old_sent->next;

    if (!first) {
        ok = 0;
        tls_stats.failed_deq_count++;
    } else {
        *v = first->v;
        head = first;
        if (tail == old_sent) tail = head;

        ok = 1;
        tls_stats.deq_count++;
        tls_stats.dequeued_values.push_back(*v);
    }

    omp_unset_lock(&q_lock);

    if (ok) free_node(old_sent);
    return ok;
}