#pragma once
#include <vector>

typedef int value_t;
typedef struct thread_stats {
    // ---------------- Benchmark stats ----------------
    unsigned long enq_count = 0;
    unsigned long deq_count = 0;
    unsigned long failed_deq_count = 0;
    unsigned long duration_ns = 0;
    std::vector<value_t> dequeued_values;

    // ---------------- Queue / allocator stats ----------------
    unsigned long freelist_pushes = 0;
    unsigned long freelist_pops = 0;
    unsigned long freelist_max_size = 0;
    unsigned long malloc_count = 0;
    unsigned long reused_count = 0;

    unsigned long successful_CAS_ops = 0; 
    unsigned long failed_CAS_ops = 0; 

    void integrate(const thread_stats& other) {
        enq_count += other.enq_count;
        deq_count += other.deq_count;
        failed_deq_count += other.failed_deq_count;
        dequeued_values.insert(
            dequeued_values.end(),
            other.dequeued_values.begin(), 
            other.dequeued_values.end()
        );
        
        freelist_pushes += other.freelist_pushes;
        freelist_pops += other.freelist_pops;
        malloc_count += other.malloc_count;
        reused_count += other.reused_count;
        freelist_max_size = std::max(
            freelist_max_size, 
            other.freelist_max_size
        );
        duration_ns += other.duration_ns;

        successful_CAS_ops += other.successful_CAS_ops;
        failed_CAS_ops += other.failed_CAS_ops;
    }
} thread_stats_t;
