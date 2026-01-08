#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t enq_count;
    uint64_t deq_count;
    uint64_t failed_deq_count;
    uint64_t duration_ns;

    uint64_t freelist_pushes;
    uint64_t freelist_pops;
    uint64_t freelist_max_size;
    uint64_t malloc_count;
    uint64_t reused_count;

    uint64_t successful_CAS_ops;
    uint64_t failed_CAS_ops;
} CThreadStats;

CThreadStats run_queue_benchmark(
    int n_threads,
    unsigned long max_enq_values,
    const int *enq_batches,
    const int *deq_batches,
    int queue_type,
    double max_duration_sec,
    bool check_dequeued_values,
    bool print_results,
    bool print_info
);

#ifdef __cplusplus
}
#endif
