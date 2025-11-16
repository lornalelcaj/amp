// Exercise 1 – Sequential queue with freelist reuse.


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef int value_t;

typedef struct {
    unsigned long freelist_pushes;
    unsigned long freelist_pops;
    unsigned long freelist_max_size;
    unsigned long malloc_count;
    unsigned long reused_count;
    char pad[64];   // avoid false sharing
} queue_stats_t;


typedef struct node {
    value_t v;
    struct node *next;
} node_t;

typedef struct freelist {
    node_t *head;
    size_t cur_size;
    size_t max_size;
} freelist_t;

typedef struct queue_t {
    node_t *head; // always points to current sentinel
    node_t *tail; // last real node (or sentinel if empty)
    freelist_t free_list;
    queue_stats_t stats;
} queue_t;

typedef queue_t *queue;


static void freelist_init(freelist_t *fl) {
    fl->head = NULL;
    fl->cur_size = 0;
    fl->max_size = 0;
}

static node_t *freelist_pop(freelist_t *fl, queue_stats_t *stats) {
    node_t *n = fl->head;
    if (n) {
        fl->head = n->next;
        fl->cur_size--;
        n->next = NULL; // clear to avoid accidental dangling links
        stats->freelist_pops++;
    }
    return n;
}

static void freelist_push(freelist_t *fl, node_t *n, queue_stats_t *stats) {
    n->next = fl->head;
    fl->head = n;
    fl->cur_size++;
    if (fl->cur_size > fl->max_size) fl->max_size = fl->cur_size;
    stats->freelist_pushes++;
}


void queue_init(queue Q) {
    node_t *sent = malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    Q->head = Q->tail = sent;
    freelist_init(&Q->free_list);

    // init stats
    Q->stats.freelist_pushes = 0;
    Q->stats.freelist_pops   = 0;
    Q->stats.freelist_max_size = 0;
    Q->stats.malloc_count    = 1;    // sentinel
    Q->stats.reused_count    = 0;
}

void queue_destroy(queue Q) {
    // free main list
    node_t *n = Q->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    // free freelist nodes
    n = Q->free_list.head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
}

static node_t *alloc_node(queue Q) {
    node_t *n = freelist_pop(&Q->free_list, &Q->stats);
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
    // reset fields (not strictly necessary but helpful)
    n->next = NULL;
    freelist_push(&Q->free_list, n, &Q->stats);
}

// --- queue ops (required signatures) ---
void enq(value_t v, queue Q) {
    node_t *n = alloc_node(Q);
    n->v = v;
    n->next = NULL;
    Q->tail->next = n;
    Q->tail = n;
}

int deq(value_t *v, queue Q) {
    node_t *old_sentinel = Q->head;
    node_t *first = old_sentinel->next;

    if (!first) return 0; // empty

    *v = first->v;
    Q->head = first; // first becomes new sentinel

    // If we removed the last real node, tail should point to the sentinel.
    if (Q->tail == first) {
        Q->tail = Q->head; // explicit and documents the invariant
    }

    free_node(Q, old_sentinel); // recycle old sentinel
    return 1;
}
