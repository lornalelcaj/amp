#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 
#include "IQueue.h"

class QueueSequentialLockThreadLocalFL : public IQueue {
    // Node structure
    typedef struct node {
        value_t v;
        struct node *next;
    } node_t;

    // Thread-local freelist structure (stack-like LIFO)
    struct FreeList {
        node_t *head;
        size_t cur_size;
        size_t max_size;

        void freelist_init();
        node_t* pop(queue_stats_t *stats);
        void push(node_t* n, queue_stats_t *stats);
    };

private:
    node_t *head;  // points to current sentinel
    node_t *tail;  // last real node or sentinel if empty
    omp_lock_t global_lock; // Global lock protecting queue operations

    // Thread-local storage for freelists
    static const int MAX_THREADS = 256;
    FreeList thread_freelists[MAX_THREADS];
    queue_stats_t thread_stats[MAX_THREADS];

public:
    void queue_init();
    void queue_destroy();
    void enq(value_t v);
    int deq(value_t *v);

private:
    node_t* alloc_node();
    void free_node(node_t *n);
    FreeList* get_thread_freelist();
    queue_stats_t* get_thread_stats();
};