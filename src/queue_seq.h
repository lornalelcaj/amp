// Exercise 1 – Sequential queue with freelist reuse.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "IQueue.h"


class QueueSequential : public IQueue {
    typedef struct node {
        value_t v;
        struct node *next;
    } node_t;

    struct FreeList {
        node_t *head;
        size_t cur_size;
        size_t max_size;

        void freelist_init();
        node_t* pop();
        void push(node_t* n);
    };

    
    
private:
    node_t *head; // always points to current sentinel
    node_t *tail; // last real node (or sentinel if empty)
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
    node_t* alloc_node();
    void free_node(node_t *n);
};
