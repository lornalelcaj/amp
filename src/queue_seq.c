// Exercise 1 – Sequential queue with freelist reuse.


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef int value_t;


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
} queue_t;

typedef queue_t *queue;


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
        n->next = NULL; // clear to avoid accidental dangling links
    }
    return n;
}

static void freelist_push(freelist_t *fl, node_t *n) {
    n->next = fl->head;
    fl->head = n;
    fl->cur_size++;
    if (fl->cur_size > fl->max_size) fl->max_size = fl->cur_size;
}


void queue_init(queue Q) {
    node_t *sent = malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    Q->head = Q->tail = sent;
    freelist_init(&Q->free_list);
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
    node_t *n = freelist_pop(&Q->free_list);
    if (!n) {
        n = malloc(sizeof *n);
        if (!n) { perror("malloc"); abort(); }
    }
    n->next = NULL;
    return n;
}

static void free_node(queue Q, node_t *n) {
    // reset fields (not strictly necessary but helpful)
    n->next = NULL;
    freelist_push(&Q->free_list, n);
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


// int main(void) {
//     queue_t q_struct;
//     queue Q = &q_struct;

//     queue_init(Q);

//     for (int i = 0; i < 5; ++i) enq(i * 10, Q);

//     value_t x;
//     printf("Dequeued values:\n");
//     while (deq(&x, Q)) printf("  %d\n", x);

//     if (!deq(&x, Q)) printf("Queue is now empty (deq returned 0).\n");

//     printf("\nFreelist max size: %zu\n", Q->free_list.max_size);

//     queue_destroy(Q);
//     return 0;
// }

