#pragma once
#include <omp.h> 
#include "thread_stats_tls.h"

typedef int value_t;

class IQueue {
    omp_lock_t stats_lock; // lock to synchronize the measurements aggregation phase
    thread_stats_t queue_stats; // global queue stats, valid after all threads have called thread_cleanup

public:
    virtual ~IQueue() = default;
    thread_stats getStats() {
        return queue_stats;
    }

    virtual void queue_init() = 0;
    virtual void queue_destroy() = 0;
    virtual void thread_prepare() {
        tls_stats = thread_stats();
    }
    virtual void thread_cleanup() {
        omp_set_lock(&this->stats_lock); 

        queue_stats.integrate(tls_stats);
        
        omp_unset_lock(&this->stats_lock);
    }
    virtual void enq(value_t v) = 0;
    virtual int deq(value_t *v) = 0;
};
