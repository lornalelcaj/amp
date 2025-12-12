#include "queue_split_lock_local_FL.h"
#include <stdio.h>
#include <assert.h>

std::atomic<QueueSplitLockLocalFL::RegNode*> QueueSplitLockLocalFL::registry_head(nullptr);

QueueSplitLockLocalFL::ThreadLocalFreeList& QueueSplitLockLocalFL::local_freelist() {
    static thread_local ThreadLocalFreeList tlf;
    static thread_local bool initialized = false;
    if (!initialized) {
        tlf.init();
        initialized = true;
        register_thread_freelist(&tlf);
    }
    return tlf;
}

void QueueSplitLockLocalFL::register_thread_freelist(ThreadLocalFreeList *fl) {
    RegNode *r = (RegNode*)malloc(sizeof(RegNode));
    if (!r) { perror("malloc"); abort(); }
    r->fl = fl;

    RegNode *old_head = registry_head.load(std::memory_order_relaxed);
    do {
        r->next = old_head;
    } while (!registry_head.compare_exchange_weak(old_head, r,
                std::memory_order_release, std::memory_order_relaxed));

    fl->reg = r;
}

void QueueSplitLockLocalFL::drain_thread_local_freelist() {
    ThreadLocalFreeList &fl = local_freelist();
    fl.drain_and_free_all();
}

void QueueSplitLockLocalFL::queue_init() {
    node_t *sent = (node_t*)malloc(sizeof(node_t));
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = this->tail = sent;

    omp_init_lock(&this->enqueue_lock);
    omp_init_lock(&this->dequeue_lock);

    this->stats_freelist_pushes.store(0);
    this->stats_freelist_pops.store(0);
    this->stats_freelist_max_size.store(0);
    this->stats_malloc_count.store(1);
    this->stats_reused_count.store(0);
}

void QueueSplitLockLocalFL::queue_destroy() {
    node_t *n = this->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    this->head = this->tail = nullptr;

    RegNode *reg_list = registry_head.exchange(nullptr, std::memory_order_acq_rel);
    while (reg_list) {
        RegNode *next_reg = reg_list->next;
        ThreadLocalFreeList *fl = reg_list->fl;
        if (fl) fl->drain_and_free_all();
        free(reg_list);
        reg_list = next_reg;
    }

    omp_destroy_lock(&this->enqueue_lock);
    omp_destroy_lock(&this->dequeue_lock);
}

QueueSplitLockLocalFL::node_t* QueueSplitLockLocalFL::get_node() {
    stats_freelist_pops.fetch_add(1, std::memory_order_relaxed);

    ThreadLocalFreeList &fl = local_freelist();
    node_t *n = fl.pop();
    if (!n) {
        n = (node_t*)malloc(sizeof(node_t));
        if (!n) { perror("malloc"); abort(); }
        stats_malloc_count.fetch_add(1, std::memory_order_relaxed);
    } else {
        stats_reused_count.fetch_add(1, std::memory_order_relaxed);
        size_t local_max = fl.max_size;
        size_t global_max = stats_freelist_max_size.load(std::memory_order_relaxed);
        while (local_max > global_max) {
            if (stats_freelist_max_size.compare_exchange_strong(global_max, local_max,
                        std::memory_order_release, std::memory_order_relaxed))
                break;
        }
    }
    n->next = NULL;
    return n;
}

void QueueSplitLockLocalFL::free_node(node_t *n) {
    stats_freelist_pushes.fetch_add(1, std::memory_order_relaxed);

    ThreadLocalFreeList &fl = local_freelist();
    fl.push(n);

    size_t local_max = fl.max_size;
    size_t global_max = stats_freelist_max_size.load(std::memory_order_relaxed);
    while (local_max > global_max) {
        if (stats_freelist_max_size.compare_exchange_strong(global_max, local_max,
                    std::memory_order_release, std::memory_order_relaxed))
            break;
    }
}

void QueueSplitLockLocalFL::enq(value_t v) {
    omp_set_lock(&this->enqueue_lock);
    node_t *n = get_node();
    n->v = v;
    n->next = NULL;
    this->tail->next = n;
    this->tail = n;
    omp_unset_lock(&this->enqueue_lock);
}

int QueueSplitLockLocalFL::deq(value_t *v) {
    int result;
    omp_set_lock(&this->dequeue_lock);

    node_t *old_sentinel = this->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0;
    } else {
        *v = first->v;
        this->head = first;
        if (this->tail == old_sentinel) this->tail = this->head;
        free_node(old_sentinel);
        result = 1;
    }

    omp_unset_lock(&this->dequeue_lock);
    return result;
}
