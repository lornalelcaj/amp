// Exercise 5 – Concurrent lock free queue with local free lists
#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <cstdlib>
#include <omp.h> 
#include <thread>
#include <atomic> // has to be the C++ variant since the C variant is not compatible with C++ compilers
#include "IQueue.h"
#include "thread_local_free_list.h"
#include "tagged_pointer.h"

class QueueLockFreeLocalFL : public IQueue {
    // Node structure and tagging information

    typedef struct alignas(OBJ_ALIGNMENT) node {
        typedef TaggedPointer<struct node> TPN;
        value_t v = -1;
        std::atomic<TPN> next_tp;

        node* getNextFL() { 
            return TPN::extract_address(next_tp.load(std::memory_order_relaxed)); 
        }
        void setNextFL(node* n) { 
            size_t tag = TPN::extract_tag(next_tp.load(std::memory_order_relaxed));
            next_tp.store(TPN::pack_pointer(n, tag + 1), std::memory_order_relaxed); 
        }
    } node_t;
    static_assert(std::atomic<struct node*>::is_always_lock_free);
    
    typedef TaggedPointer<node_t> TP;
    static_assert(sizeof(TP) == sizeof(node_t*));

public:
    typedef ThreadLocalFreeList<node_t> TLFL; // actual free list is defined in .cpp file
    
private:
    std::atomic<TP> head_tp; //  points to current sentinel, tagged to mitigate memory leak ABA problem
    std::atomic<TP> tail_tp; // last real node or sentinel if empty, tagged to mitigate queue splitting ABA problem
    omp_lock_t queue_stats_lock;
    
    static_assert(std::atomic<TP>::is_always_lock_free);

public:
    // Initialize queue and lock 
    void queue_init();

    // Destroy queue and lock 
    void queue_destroy();

    // register thread local free lists
    void thread_prepare() override;

    // destroy thread local free lists
    void thread_cleanup() override;

    // Enqueue protected by global lock
    void enq(value_t v);

    // Dequeue protected by global lock 
    int deq(value_t *v);

private:
    // utility function to move the tail to the next_tp node
    void helpMoveTail(QueueLockFreeLocalFL::TP &tailTptr, QueueLockFreeLocalFL::node_t *next_tp);

    // Get a (potentially reused) node to use in the queue
    node_t* get_node();
    
    // Discard a node dequeued from the queue (by saving it to the free queue)
    void free_node(node_t* n);
    
    // allocates a new node to the heap which is properly memory alligned
    // and has tagged ptr = NULL
    static node_t* allocate_node();
};
