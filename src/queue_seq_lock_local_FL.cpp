#include <string.h>
#include "queue_seq_lock_local_FL.h"
// Freelist methods - simple stack (LIFO) implementation
void QueueSequentialLockThreadLocalFL::FreeList::freelist_init() {
    this->head = NULL;
    this->cur_size = 0;
    this->max_size = 0;
}

QueueSequentialLockThreadLocalFL::node_t* 
QueueSequentialLockThreadLocalFL::FreeList::pop(queue_stats_t *stats) {
    node_t *n = this->head;
    if (n) {
        this->head = n->next;
        this->cur_size--;
        n->next = NULL;
        stats->freelist_pops++;
    }
    return n;
}

void QueueSequentialLockThreadLocalFL::FreeList::push(node_t *n, queue_stats_t *stats) {
    n->next = this->head;
    this->head = n;
    this->cur_size++;
    if (this->cur_size > this->max_size) {
        this->max_size = this->cur_size;
    }
    if (this->cur_size > stats->freelist_max_size) {
        stats->freelist_max_size = this->cur_size;
    }
    stats->freelist_pushes++;
}

// Get thread-specific freelist (no synchronization needed)
QueueSequentialLockThreadLocalFL::FreeList* 
QueueSequentialLockThreadLocalFL::get_thread_freelist() {
    int tid = omp_get_thread_num();
    return &thread_freelists[tid];
}

// Get thread-specific stats (no synchronization needed)
queue_stats_t* QueueSequentialLockThreadLocalFL::get_thread_stats() {
    int tid = omp_get_thread_num();
    return &thread_stats[tid];
}

// Initialize queue, lock, and all thread-local freelists
void QueueSequentialLockThreadLocalFL::queue_init() {
    // Create sentinel node
    node_t *sent = (node_t*)malloc(sizeof(*sent));
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = this->tail = sent;
    
    // Initialize global lock (using OpenMP lock functions)
    omp_init_lock(&this->global_lock);

    // Initialize all thread-local freelists and stats
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_freelists[i].freelist_init();
        memset(&thread_stats[i], 0, sizeof(queue_stats_t));
    }
    
    // Initialize global stats (inherited from IQueue)
    memset(&this->stats, 0, sizeof(queue_stats_t));
    this->stats.malloc_count = 1;  // count the sentinel
}

// Destroy queue and lock
void QueueSequentialLockThreadLocalFL::queue_destroy() {
    // Free main queue
    node_t *n = this->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    
    // Free all thread-local freelists and aggregate stats
    for (int i = 0; i < MAX_THREADS; i++) {
        n = thread_freelists[i].head;
        while (n) {
            node_t *tmp = n->next;
            free(n);
            n = tmp;
        }
        
        // Aggregate thread stats into global stats
        this->stats.freelist_pushes += thread_stats[i].freelist_pushes;
        this->stats.freelist_pops += thread_stats[i].freelist_pops;
        if (thread_stats[i].freelist_max_size > this->stats.freelist_max_size) {
            this->stats.freelist_max_size = thread_stats[i].freelist_max_size;
        }
        this->stats.malloc_count += thread_stats[i].malloc_count;
        this->stats.reused_count += thread_stats[i].reused_count;
    }
    
    // Destroy the lock
    omp_destroy_lock(&this->global_lock);
}

// Allocate node from thread-local freelist (NO SYNCHRONIZATION NEEDED)
QueueSequentialLockThreadLocalFL::node_t* 
QueueSequentialLockThreadLocalFL::alloc_node() {
    FreeList *fl = get_thread_freelist();
    queue_stats_t *stats = get_thread_stats();
    
    node_t *n = fl->pop(stats);
    if (!n) {
        // Freelist empty, allocate from global memory (malloc)
        n = (node_t*)malloc(sizeof(node_t));
        if (!n) { perror("malloc"); abort(); }
        stats->malloc_count++;
    } else {
        stats->reused_count++;
    }
    n->next = NULL;
    return n;
}

// Free node to thread-local freelist (NO SYNCHRONIZATION NEEDED)
void QueueSequentialLockThreadLocalFL::free_node(node_t *n) {
    FreeList *fl = get_thread_freelist();
    queue_stats_t *stats = get_thread_stats();
    
    n->next = NULL;
    fl->push(n, stats);
}

// Enqueue protected by global lock
// Lock functionality used: OpenMP locks (omp_lock_t, omp_set_lock, omp_unset_lock)
void QueueSequentialLockThreadLocalFL::enq(value_t v) {
    // Allocate node from thread-local freelist (NO LOCK NEEDED)
    // This is done outside the critical section for better performance
    node_t *n = this->alloc_node();
    n->v = v;
    n->next = NULL;
    
    // ACQUIRE LOCK for queue manipulation
    omp_set_lock(&this->global_lock);
    
    // CRITICAL SECTION: Update queue structure
    this->tail->next = n;
    this->tail = n;
    
    // RELEASE LOCK
    omp_unset_lock(&this->global_lock);
}

// Dequeue protected by global lock
// Lock functionality used: OpenMP locks (omp_lock_t, omp_set_lock, omp_unset_lock)
int QueueSequentialLockThreadLocalFL::deq(value_t *v) {
    int result;
    node_t *old_sentinel = NULL;
    
    // ACQUIRE LOCK
    omp_set_lock(&this->global_lock);
    
    // CRITICAL SECTION: Check and remove from queue
    old_sentinel = this->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0;  // Queue empty
    } else {
        *v = first->v;
        this->head = first;  // first becomes new sentinel

        if (this->tail == old_sentinel) {
            this->tail = this->head;
        }
        result = 1;
    }
    
    // RELEASE LOCK
    omp_unset_lock(&this->global_lock);
    
    // Free old sentinel to thread-local freelist (NO LOCK NEEDED)
    // This is done outside the critical section for better performance
    if (result == 1) {
        this->free_node(old_sentinel);
    }
    
    return result;
}