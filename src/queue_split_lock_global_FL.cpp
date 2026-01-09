// Exercise 4 – Concurrent queue with two locks one for enqueueing and one for dequeueing.
// This version has a global free queue
#include "queue_split_lock_global_FL.h"
#include <stdio.h>

//  Freelist Helpers, Left Sequential, Protected by Queue Lock 
void QueueSplitLockGlobalFL::FreeList::freelist_init() {
    node_t *sent = (node_t*)malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = sent;
    this->tail = sent;
    atomic_init(&this->cur_size, 0);
    atomic_init(&this->max_size, 0);
}

QueueSplitLockGlobalFL::node_t* QueueSplitLockGlobalFL::FreeList::deq() {
    node_t *n = this->head;
    // check if queue is empty
    if (!n->next) return NULL;
    
    this->head = n->next;
    atomic_fetch_add(&this->cur_size, -1);
    
    n->next = NULL;
    return n;
}

void QueueSplitLockGlobalFL::FreeList::enq(node_t* n) {
    n->next = NULL;
    this->tail->next = n;
    this->tail = n;
    atomic_fetch_add(&this->cur_size, 1);
    size_t current = atomic_load(&this->cur_size);
    size_t maximum = atomic_load(&this->max_size);
    
    while(current > maximum) {
        // if CEX fails the value of maximum is overwritten with its actual value
        if (atomic_compare_exchange_strong(&this->max_size, &maximum, current)) {
            tls_stats.successful_CAS_ops++;
            break;
        }
        else {
            tls_stats.failed_CAS_ops++;
        }
    }
}



// Initialize queue and locks
void QueueSplitLockGlobalFL::queue_init() {
    IQueue::queue_init();
    node_t *sent = (node_t*)malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = this->tail = sent;
    this->free_list.freelist_init();
    
    // Initialize locks
    omp_init_lock(&this->enqueue_lock);
    omp_init_lock(&this->dequeue_lock);
}

// Destroy queue and lock 
void QueueSplitLockGlobalFL::queue_destroy() {
    // Free main list
    node_t *n = this->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    // Free freelist nodes
    n = this->free_list.head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    // Destroy the locks
    omp_destroy_lock(&this->enqueue_lock); 
    omp_destroy_lock(&this->dequeue_lock); 
}

// Get/Free nodes remain as sequential helpers, implicitly protected
// returns either a node from the free list or creates a new one
QueueSplitLockGlobalFL::node_t* QueueSplitLockGlobalFL::get_node() {
    node_t *n = this->free_list.deq();
    if (!n) {
        n = (node_t*)malloc(sizeof(node_t));
        if (!n) { perror("malloc"); abort(); }
        tls_stats.malloc_count++;
    } else {
        tls_stats.freelist_pops++;
        tls_stats.reused_count++;
    }
    n->next = NULL;
    return n;
}

void QueueSplitLockGlobalFL::free_node(node_t *n) {
    tls_stats.freelist_pushes++;
    
    n->next = NULL;
    this->free_list.enq(n);

    // update stats (works because only one thread can be here at a time)
    size_t max_fl = atomic_load(&this->free_list.max_size);
    if (max_fl > tls_stats.freelist_max_size) {
        tls_stats.freelist_max_size = max_fl;
    }
}



// Enqueue protected by enqueue lock 
void QueueSplitLockGlobalFL::enq(value_t v) {
    omp_set_lock(&this->enqueue_lock); 

    node_t *n = get_node();

    // since only one thread at a time can enqueue, tail will always point to the last node
    n->v = v;
    n->next = NULL;
    this->tail->next = n;
    this->tail = n;
     
    omp_unset_lock(&this->enqueue_lock);
}

// Dequeue protected by dequeue lock 
int QueueSplitLockGlobalFL::deq(value_t *v) {
    int result;
    
    omp_set_lock(&this->dequeue_lock);

    node_t *old_sentinel = this->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0; // empty
    } else {
        *v = first->v;
        this->head = first; // first becomes new sentinel

        // help enqueueing thread in case it sleeps 
        if (this->tail == old_sentinel) {
            omp_set_lock(&enqueue_lock);
            if (this->tail == old_sentinel) {
                this->tail = this->head;
            }
            omp_unset_lock(&enqueue_lock);
        }

        free_node(old_sentinel); // recycle old sentinel
        result = 1; // success
    }
     
    omp_unset_lock(&this->dequeue_lock);
    return result;
}
