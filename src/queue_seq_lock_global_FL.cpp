// Exercise 2 – Concurrent queue with a single global lock.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 
#include "queue_seq_lock_global_FL.h"

//  Freelist Helpers, Left Sequential  Protected by Queue Lock 
void QueueSequentialLockGlobalFL::FreeList::freelist_init() {
    this->head = NULL;
    this->cur_size = 0;
    this->max_size = 0;
}

QueueSequentialLockGlobalFL::node_t* QueueSequentialLockGlobalFL::FreeList::pop() {
    node_t *n = this->head;
    if (n) {
        this->head = n->next;
        this->cur_size--;
        n->next = NULL; // clear to avoid accidental dangling links
        tls_stats.freelist_pops++;
    }
    return n;
}

void QueueSequentialLockGlobalFL::FreeList::push(node_t *n) {
    n->next = this->head;
    this->head = n;
    this->cur_size++;
    if (this->cur_size > this->max_size) {
        this->max_size = this->cur_size;
        if (this->cur_size > tls_stats.freelist_max_size) {
            tls_stats.freelist_max_size = this->cur_size;
        }
    }

    tls_stats.freelist_pushes++;
}


// Initialize queue and lock 
void QueueSequentialLockGlobalFL::queue_init() {
    node_t *sent = (node_t*)malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = this->tail = sent;
    this->free_list.freelist_init();
    
    // Initialize the lock
    omp_init_lock(&this->global_lock); // <-- INITIALIZE LOCK
}

// Destroy queue and lock 
void QueueSequentialLockGlobalFL::queue_destroy() {
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
    
    // Destroy the lock
    omp_destroy_lock(&this->global_lock); 
}

// Alloc/Free nodes remain as sequential helpers, implicitly protected
QueueSequentialLockGlobalFL::node_t* QueueSequentialLockGlobalFL::alloc_node() {
    node_t *n = this->free_list.pop();
    if (!n) {
        n = (node_t*)malloc(sizeof(node_t));
        if (!n) { perror("malloc"); abort(); }
        tls_stats.malloc_count++;
    } else {
        tls_stats.reused_count++;
    }
    n->next = NULL;
    return n;
}

void QueueSequentialLockGlobalFL::free_node(node_t *n) {
    n->next = NULL;
    this->free_list.push(n);
}



// Enqueue protected by global lock 
void QueueSequentialLockGlobalFL::enq(value_t v) {
    // ACQUIRE LOCK
    omp_set_lock(&this->global_lock); 
    
    // CRITICAL SECTION START (Sequential Enqueue Logic)
    node_t *n = this->alloc_node();
    n->v = v;
    n->next = NULL;
    this->tail->next = n;
    this->tail = n;
     
    
    // RELEASE LOCK
    omp_unset_lock(&this->global_lock);
}

// Dequeue protected by global lock 
int QueueSequentialLockGlobalFL::deq(value_t *v) {
    int result;
    
    // ACQUIRE LOCK
    omp_set_lock(&this->global_lock);
    
    // CRITICAL SECTION START (Sequential Dequeue Logic)
    node_t *old_sentinel = this->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0; // empty
    } else {
        *v = first->v;
        this->head = first; // first becomes new sentinel

        if (this->tail == old_sentinel) {
            this->tail = this->head;
        }

        this->free_node(old_sentinel); // recycle old sentinel
        result = 1;
    }
     
    
    // RELEASE LOCK
    omp_unset_lock(&this->global_lock);
    return result; // Return result after releasing the lock
}
