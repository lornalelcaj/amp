// Exercise 2 – Concurrent queue with a single global lock.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h> 

typedef int value_t;


// Node structure
typedef struct node {
    value_t v;
    struct node *next;
} node_t;

// Freelist structure
typedef struct freelist {
    node_t *head;
    size_t cur_size;
    size_t max_size;
} freelist_t;

// GLOBAL LOCK ADDED
typedef struct queue_t {
    node_t *head; //  points to current sentinel
    node_t *tail; // last real node or sentinel if empty
    freelist_t free_list;
    omp_lock_t global_lock; // Global lock to protect the entire queue
} queue_t;

typedef queue_t *queue;

//  Freelist Helpers, Left Sequential  Protected by Queue Lock 

static void freelist_init(freelist_t *fl) {
    fl->head = NULL;
    fl->cur_size = 0;
    fl->max_size = 0;
}

static node_t *freelist_pop(freelist_t *fl) {
    node_t *n = fl->head;
    if (n) {
        fl->head = n->next;
        fl->cur_size--;
        n->next = NULL; 
    }
    return n;
}

static void freelist_push(freelist_t *fl, node_t *n) {
    n->next = fl->head;
    fl->head = n;
    fl->cur_size++;
    if (fl->cur_size > fl->max_size) fl->max_size = fl->cur_size;
}



// Initialize queue and lock 
void queue_init(queue Q) {
    node_t *sent = malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    Q->head = Q->tail = sent;
    freelist_init(&Q->free_list);
    
    // Initialize the lock
    omp_init_lock(&Q->global_lock); // <-- INITIALIZE LOCK
}

// Destroy queue and lock 
void queue_destroy(queue Q) {
    // Free main list
    node_t *n = Q->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    // Free freelist nodes
    n = Q->free_list.head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    
    // Destroy the lock
    omp_destroy_lock(&Q->global_lock); 
}

// Alloc/Free nodes remain as sequential helpers, implicitly protected
static node_t *alloc_node(queue Q) {
    node_t *n = freelist_pop(&Q->free_list);
    if (!n) {
        n = malloc(sizeof *n);
        if (!n) { perror("malloc"); abort(); }
    }
    n->next = NULL;
    return n;
}

static void free_node(queue Q, node_t *n) {
    n->next = NULL;
    freelist_push(&Q->free_list, n);
}



// Enqueue protected by global lock 
void enq(value_t v, queue Q) {
    // ACQUIRE LOCK
    omp_set_lock(&Q->global_lock); 
    
    // CRITICAL SECTION START (Sequential Enqueue Logic)
    node_t *n = alloc_node(Q);
    n->v = v;
    n->next = NULL;
    Q->tail->next = n;
    Q->tail = n;
     
    
    // RELEASE LOCK
    omp_unset_lock(&Q->global_lock);
}

// Dequeue protected by global lock 
int deq(value_t *v, queue Q) {
    int result;
    
    // ACQUIRE LOCK
    omp_set_lock(&Q->global_lock);
    
    // CRITICAL SECTION START (Sequential Dequeue Logic)
    node_t *old_sentinel = Q->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0; // empty
    } else {
        *v = first->v;
        Q->head = first; // first becomes new sentinel

        if (Q->tail == old_sentinel) {
            Q->tail = Q->head;
        }

        free_node(Q, old_sentinel); // recycle old sentinel
        result = 1;
    }
     
    
    // RELEASE LOCK
    omp_unset_lock(&Q->global_lock);
    return result; // Return result after releasing the lock
}


// int main(void) {
//     // This demo is sequential, but the functions are now thread-safe.
//     queue_t q_struct;
//     queue Q = &q_struct;

//     queue_init(Q);

//     for (int i = 0; i < 5; ++i) enq(i * 10, Q);

//     value_t x;
//     printf("Dequeued values:\n");
//     while (deq(&x, Q)) printf("  %d\n", x);

//     if (!deq(&x, Q)) printf("Queue is now empty (deq returned 0).\n");

//     printf("\nFreelist max size: %zu\n", Q->free_list.max_size);

//     queue_destroy(Q);
//     return 0;
// }
