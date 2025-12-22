# pragma once
#include <cstdlib>
#include <stdio.h>
#include <cassert>

typedef struct {
    size_t max_size = 0;
    size_t num_pushes = 0;
    size_t num_pops = 0;
} free_list_stats_t;

/**
 * Manages a free list as a stack where ony one thread at a time pops and pushes elements
 * 
 * Expects node_t to provide the following functions to get and set a nodes next pointer:
 * virtual node_t* getNextFL(); 
 * virtual void setNextFL(node_t*); 
 * 
 */
template <typename node_t> class ThreadLocalFreeList {
private:
    node_t* top;
    size_t size;

    /*
        Dummy element to signal the end of the free list.
        It makes it so that only freshly allocated nodes and those at the end of the main queue point to NULL.
        This is to make it impossible for a node to be accidentally enqueued into the free list, 
        which should solve the lost nodes ABA problem.
    */ 
    node_t nill; 

public:
    free_list_stats_t stats;

    ThreadLocalFreeList() {
        top = &nill;
        size = 0;
        stats.max_size = 0;
    }

    inline node_t* pop() {
        if (top == &nill) return NULL;
        node_t* n = top;
        top = top->getNextFL();
        size--;
        stats.num_pops++;
        return n;
    }

    inline void push(node_t* n) {
        n->setNextFL(top);
        top = n;
        size++;
        stats.num_pushes++;
        if (size > stats.max_size) 
            stats.max_size = size;
    }

    // destroys all elements in the free list
    ~ThreadLocalFreeList() {
        node_t* cur = top;
        size_t freed = 0;
        while (cur != &nill) {
            node_t* tmp = cur->getNextFL();
            free(cur);
            cur = tmp;
            freed++;
        }

        // check if lost node ABA problem occured
        if (freed != size) {
            fprintf(stderr, "Freelist of size %lu contained %lu elements.\n", size, freed);
        }
        top = NULL;
        size = 0;
    }
};
