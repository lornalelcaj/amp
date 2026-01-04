// Exercise 1 – Sequential queue with freelist reuse.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "queue_seq.h"

void QueueSequential::FreeList::freelist_init() {
    this->head = NULL;
    this->cur_size = 0;
    this->max_size = 0;
}

QueueSequential::node_t* QueueSequential::FreeList::pop() {
    node_t *n = this->head;
    if (n) {
        this->head = n->next;
        this->cur_size--;
        n->next = NULL; // clear to avoid accidental dangling links
        tls_stats.freelist_pops++;
    }
    return n;
}

void QueueSequential::FreeList::push(node_t *n) {
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


void QueueSequential::queue_init() {
    IQueue::queue_init();
    node_t *sent = (node_t*)malloc(sizeof *sent);
    if (!sent) { perror("malloc"); abort(); }
    sent->next = NULL;
    this->head = this->tail = sent;
    this->free_list.freelist_init();
}

void QueueSequential::queue_destroy() {
    // free main list
    node_t *n = this->head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
    // free freelist nodes
    n = this->free_list.head;
    while (n) {
        node_t *tmp = n->next;
        free(n);
        n = tmp;
    }
}

QueueSequential::node_t* QueueSequential::alloc_node() {
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

void QueueSequential::free_node(node_t *n) {
    // reset fields (not strictly necessary but helpful)
    n->next = NULL;
    this->free_list.push(n);
}

// --- queue ops (required signatures) ---
void QueueSequential::enq(value_t v) {
    node_t *n = this->alloc_node();
    n->v = v;
    n->next = NULL;
    this->tail->next = n;
    this->tail = n;
}

int QueueSequential::deq(value_t *v) {
    node_t *old_sentinel = this->head;
    node_t *first = old_sentinel->next;

    if (!first) return 0; // empty

    *v = first->v;
    this->head = first; // first becomes new sentinel

    // If we removed the last real node, tail should point to the sentinel.
    if (this->tail == first) {
        this->tail = this->head; // explicit and documents the invariant
    }

    this->free_node(old_sentinel); // recycle old sentinel
    return 1;
}
