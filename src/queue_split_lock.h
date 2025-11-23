// Exercise 3 – Concurrent queue with two locks, one for enqueueing and one for dequeueing.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 
#include <stdatomic.h>

typedef int value_t;

typedef struct {
    unsigned long freelist_pushes;
    unsigned long freelist_pops;
    unsigned long freelist_max_size;
    unsigned long malloc_count;
    unsigned long reused_count;
    char pad[64];   // avoid false sharing
} queue_stats_t;

// Node structure
typedef struct node {
    value_t v;
    struct node *next;
} node_t;

// Freelist structure
typedef struct freelist {
    node_t *head;
    node_t* tail;
    _Atomic size_t cur_size;
    _Atomic size_t max_size;
} freelist_t;


typedef struct queue_t {
    node_t *head; //  points to current sentinel
    node_t* tail; // last real node or sentinel if empty
    freelist_t free_list;
    omp_lock_t enqueue_lock; // Global lock to protect enqueueing
    omp_lock_t dequeue_lock; // Global lock to protect dequeueing
    queue_stats_t stats;
} queue_t;

typedef queue_t *queue;

// Initialize queue and lock 
void queue_init(queue Q);

// Destroy queue and lock 
void queue_destroy(queue Q);

// Enqueue protected by global lock 
void enq(value_t v, queue Q);

// Dequeue protected by global lock 
int deq(value_t *v, queue Q);