// Exercise 3 – Concurrent queue with two locks, one for enqueueing and one for dequeueing.
#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 
#include <atomic> // has to be the C++ variant since the C variant is not compatible with C++ compilers
#include "IQueue.h"

class QueueSplitLockGlobalFL : public IQueue {
    // Node structure
    typedef struct node {
        value_t v;
        struct node *next;
    } node_t;

    // Freelist structure
    struct FreeList {
        node_t *head;
        node_t* tail;
        std::atomic<size_t> cur_size;
        std::atomic<size_t> max_size;

        void freelist_init();
        node_t* deq();
        void enq(node_t* n);
    };

private:
    node_t *head; //  points to current sentinel
    node_t* tail; // last real node or sentinel if empty
    omp_lock_t enqueue_lock; // Global lock to protect enqueueing
    omp_lock_t dequeue_lock; // Global lock to protect dequeueing
    FreeList free_list;
    

public:
    // Initialize queue and lock 
    void queue_init();

    // Destroy queue and lock 
    void queue_destroy();

    // Enqueue protected by global lock 
    void enq(value_t v);

    // Dequeue protected by global lock 
    int deq(value_t *v);

private:
    node_t* get_node();
    void free_node(node_t *n);
};
