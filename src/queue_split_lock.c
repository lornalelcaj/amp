// Exercise 2 – Concurrent queue with a single global lock.
#include "queue_split_lock.h"


//  Freelist Helpers, Left Sequential, Protected by Queue Lock 

static void freelist_init(freelist_t *fl) {
    node_t *sent = malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    fl->head = sent;
    fl->tail = sent;
    atomic_init(&fl->cur_size, 0);
    atomic_init(&fl->max_size, 0);
}

static node_t *freelist_deq(freelist_t *fl) {
    node_t *n = fl->head;
    // check if queue is empty
    if (!n->next) return NULL;
    
    fl->head = n->next;
    atomic_fetch_add(&fl->cur_size, -1);
    
    n->next = NULL;
    return n;
}

static void freelist_enq(freelist_t *fl, node_t *n) {
    n->next = NULL;
    fl->tail->next = n;
    fl->tail = n;
    atomic_fetch_add(&fl->cur_size, 1);
    size_t current = atomic_load(&fl->cur_size);
    size_t maximum = atomic_load(&fl->max_size);
    
    while(current > maximum) {
        // if CEX fails the value of maximum is overwritten with its actual value
        if (atomic_compare_exchange_strong(&fl->max_size, &maximum, current))
            break;
    }
}



// Initialize queue and locks
void queue_init(queue Q) {
    node_t *sent = malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    Q->head = Q->tail = sent;
    freelist_init(&Q->free_list);
    
    // Initialize locks
    omp_init_lock(&Q->enqueue_lock);
    omp_init_lock(&Q->dequeue_lock);

    // Initialize stats
    Q->stats.freelist_pushes = 0;
    Q->stats.freelist_pops = 0;
    Q->stats.freelist_max_size = 0;
    Q->stats.malloc_count = 2; // sentinels of queue and free queue
    Q->stats.reused_count = 0;
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
    printf("Delete Free list\n");
    // Free freelist nodes
    n = Q->free_list.head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    printf("Delete locks\n");
    // Destroy the locks
    omp_destroy_lock(&Q->enqueue_lock); 
    omp_destroy_lock(&Q->dequeue_lock); 
}

// Get/Free nodes remain as sequential helpers, implicitly protected
// returns either a node from the free list or creates a new one
static node_t *get_node(queue Q) {
    Q->stats.freelist_pops++;
    node_t *n = freelist_deq(&Q->free_list);
    if (!n) {
        n = malloc(sizeof *n);
        if (!n) { perror("malloc"); abort(); }
        Q->stats.malloc_count++;
    } else {
        Q->stats.reused_count++;
    }
    n->next = NULL;
    return n;
}

static void free_node(queue Q, node_t *n) {
    Q->stats.freelist_pushes++;
    n->next = NULL;
    freelist_enq(&Q->free_list, n);

    // update stats (works because only one thread can be here at a time)
    if(Q->free_list.max_size > atomic_load(&Q->stats.freelist_max_size)) {
        Q->stats.freelist_max_size = atomic_load(&Q->free_list.max_size);
    }
}



// Enqueue protected by enqueue lock 
void enq(value_t v, queue Q) {
    omp_set_lock(&Q->enqueue_lock); 

    node_t *n = get_node(Q);

    // since only one thread at a time can enqueue, tail will always point to the last node
    n->v = v;
    n->next = NULL;
    Q->tail->next = n;
    Q->tail = n;
     
    omp_unset_lock(&Q->enqueue_lock);
}

// Dequeue protected by dequeue lock 
int deq(value_t *v, queue Q) {
    int result;
    
    omp_set_lock(&Q->dequeue_lock);

    node_t *old_sentinel = Q->head;
    node_t *first = old_sentinel->next;

    if (!first) {
        result = 0; // empty
    } else {
        *v = first->v;
        Q->head = first; // first becomes new sentinel

        // help enqueueing thread in case it sleeps 
        if (Q->tail == old_sentinel) {
            Q->tail = Q->head;
        }

        free_node(Q, old_sentinel); // recycle old sentinel
        result = 1; // success
    }
     
    omp_unset_lock(&Q->dequeue_lock);
    return result;
}
