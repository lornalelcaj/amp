#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <omp.h>
#include <atomic>
#include "IQueue.h"

class QueueSplitLockLocalFL : public IQueue {
public:
    typedef struct node {
        value_t v;
        struct node *next;
    } node_t;

private:
    // Forward declaration so ThreadLocalFreeList can have a RegNode* member
    struct RegNode;

    struct ThreadLocalFreeList {
        node_t *top;
        size_t size;
        size_t max_size;
        RegNode *reg;  // now valid

        inline void init() {
            top = nullptr;
            size = 0;
            max_size = 0;
            reg = nullptr;
        }

        inline node_t* pop() {
            if (!top) return nullptr;
            node_t* n = top;
            top = top->next;
            --size;
            return n;
        }

        inline void push(node_t* n) {
            n->next = top;
            top = n;
            ++size;
            if (size > max_size) max_size = size;
        }

        inline size_t drain_and_free_all() {
            size_t freed = 0;
            node_t* cur = top;
            top = nullptr;
            size = 0;
            while (cur) {
                node_t* tmp = cur->next;
                free(cur);
                cur = tmp;
                ++freed;
            }
            return freed;
        }
    };

    struct RegNode {  // define RegNode after ThreadLocalFreeList
        ThreadLocalFreeList *fl;
        RegNode *next;
    };

    static std::atomic<RegNode*> registry_head;

    node_t *head;
    node_t *tail;
    omp_lock_t enqueue_lock;
    omp_lock_t dequeue_lock;

    std::atomic<size_t> stats_freelist_pushes;
    std::atomic<size_t> stats_freelist_pops;
    std::atomic<size_t> stats_freelist_max_size;
    std::atomic<size_t> stats_malloc_count;
    std::atomic<size_t> stats_reused_count;

public:
    void queue_init();
    void queue_destroy();
    void enq(value_t v);
    int deq(value_t *v);
    static void drain_thread_local_freelist();

private:
    node_t* get_node();
    void free_node(node_t *n);
    static ThreadLocalFreeList& local_freelist();
    static void register_thread_freelist(ThreadLocalFreeList *fl);
};
