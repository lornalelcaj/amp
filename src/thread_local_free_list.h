# pragma once
#include <cstdlib>
#include <stdio.h>

typedef struct {
    size_t max_size = 0;
    size_t num_pushes = 0;
    size_t num_pops = 0;
} free_list_stats_t;

/**
 * Manages a free list as a stack where ony one thread at a time pops and pushes elements
 */
template <typename node_t> class ThreadLocalFreeList {
private:
    node_t *top;
    size_t size;

public:
    free_list_stats_t stats;

    ThreadLocalFreeList() {
        top = NULL;
        size = 0;
        stats.max_size = 0;
    }

    inline node_t* pop() {
        if (!top) return NULL;
        node_t* n = top;
        top = top->next.load();
        size--;
        stats.num_pops++;
        assert(size >= 0);
        return n;
    }

    inline void push(node_t* n) {
        n->next.store(top);
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
        while (cur) {
            node_t* tmp = cur->next.load();
            free(cur);
            cur = tmp;
            freed++;
        }

        // check if lost node ABA problem occured
        if (freed > size) {
            fprintf(stderr, "Freelist of size %lu contained %lu elements.\n");
        }
        top = NULL;
        size = 0;
    }
};