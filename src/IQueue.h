#pragma once


typedef int value_t;

typedef struct {
    unsigned long freelist_pushes;
    unsigned long freelist_pops;
    unsigned long freelist_max_size;
    unsigned long malloc_count;
    unsigned long reused_count;
    char pad[64];   // avoid false sharing
} queue_stats_t;

class IQueue {
public:
    queue_stats_t stats;
    virtual ~IQueue() = default; 

    // Initialize queue and lock 
    virtual void queue_init() = 0;

    // Destroy queue and lock 
    virtual void queue_destroy() = 0;

    // Enqueue protected by global lock 
    virtual void enq(value_t v) = 0;

    // Dequeue protected by global lock 
    virtual int deq(value_t *v) = 0;
};