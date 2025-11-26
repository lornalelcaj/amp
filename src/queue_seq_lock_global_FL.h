// Exercise 2 – Concurrent queue with a single global lock.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 
#include "IQueue.h"

class QueueSequentialLockGlobalFL : public IQueue {
    // Node structure
    typedef struct node {
        value_t v;
        struct node *next;
    } node_t;

    // Freelist structure
    struct FreeList {
        node_t *head;
        size_t cur_size;
        size_t max_size;

        void freelist_init();
        node_t* pop(queue_stats_t *stats);
        void push(node_t* n, queue_stats_t *stats);
    };

private:
    // GLOBAL LOCK ADDED
    node_t *head; //  points to current sentinel
    node_t *tail; // last real node or sentinel if empty
    FreeList free_list;
    omp_lock_t global_lock; // Global lock to protect the entire queue

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
    node_t* alloc_node();
    void free_node(node_t *n);
};