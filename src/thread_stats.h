#pragma once
#include <vector>
#include <pthread.h>
#include <cstddef>

// Forward declaration
class IQueue;

typedef struct alignas(64) thread_stats_t {
    // ---------------- Benchmark stats ----------------
    unsigned long enq_count = 0;
    unsigned long deq_count = 0;
    unsigned long failed_deq_count = 0;
    std::vector<value_t>* dequeued_values = nullptr;

    // ---------------- Queue / allocator stats ----------------
    unsigned long freelist_pushes = 0;
    unsigned long freelist_pops = 0;
    unsigned long freelist_max_size = 0;
    unsigned long malloc_count = 0;
    unsigned long reused_count = 0;

    // ---------------- Thread arguments ----------------
    int thread_id = -1;
    int n_threads = 0;
    int enq_batch = 0;
    int deq_batch = 0;
    int repetitions = 0;
    int interval_start = 0;
    int interval_end = 0;
    unsigned long total_values = 0;

    IQueue* Q = nullptr;
    pthread_barrier_t* barrier = nullptr;

    // Padding to avoid false sharing
    char pad[64];
} thread_stats_t;

extern thread_local thread_stats_t* tls_stats;
