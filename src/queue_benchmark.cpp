// bench.c - instrumented benchmark
// Build: g++ -fopenmp \
//    queue_benchmark.cpp \
//    queue_seq.cpp \
//    queue_seq_lock_global_FL.cpp \
//    queue_split_lock_global_FL.cpp \
//    -o queue_benchmark

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>

#include "queue_seq.h"
#include "queue_seq_lock_global_FL.h"
#include "queue_split_lock_global_FL.h"

// ------------------ Thread stats (avoid false sharing) --------------

typedef struct {
    unsigned long enq_count;
    unsigned long deq_count;
    unsigned long failed_deq_count;
    char pad[64];
} thread_stats_t;

typedef struct {
    int start;
    int end;    // exclusive
} interval_t;


// ------------------ Timing ---------------------

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}


// ------------------ Worker Thread ---------------------

typedef struct {
    IQueue* Q;
    int enq_batch;
    int deq_batch;
    int repetitions;
    interval_t interval;
    thread_stats_t *stats;
} thread_arg_t;

void* worker(void *arg_) {
    thread_arg_t *arg = (thread_arg_t*)arg_;
    value_t tmp;
    int val = arg->interval.start;

    for (int rep = 0; rep < arg->repetitions; rep++) {

        for (int i = 0; i < arg->enq_batch; i++) {
            arg->Q->enq(val++);
            arg->stats->enq_count++;
        }

        for (int i = 0; i < arg->deq_batch; i++) {
            if (arg->Q->deq(&tmp))
                arg->stats->deq_count++;
            else
                arg->stats->failed_deq_count++;
        }
    }
    free(arg_);
    return NULL;
}


enum queue_types {
    SEQUENTIAL, 
    ONE_LOCK_LOCAL_FQ, 
    ONE_LOCK_GLOBAL_FQ, 
    TWO_LOCKS_LOCAL_FQ, 
    TWO_LOCKS_GLOBAL_FQ,
    LOCK_FREE 
};

// ------------------ Benchmark Driver ---------------------
int main(int argc, char **argv) {
    int n_threads = 4;
    int repetitions = 1000000;
    int enq_batch = 10;
    int deq_batch = 10;
    queue_types queue_type = SEQUENTIAL;
    
    //TODO variable batch size
    
    int opt;
    while ((opt = getopt(argc, argv, "t:r:E:D:Q:")) != -1) {
        switch(opt) {
            case 't': n_threads = atoi(optarg); break;
            case 'r': repetitions = atoi(optarg); break;
            case 'E': enq_batch = atoi(optarg); break;
            case 'D': deq_batch = atoi(optarg); break;
            case 'Q': queue_type = queue_types(atoi(optarg)); break;
        }
    }

    IQueue* Q = NULL;
    switch (queue_type)
    {
    case SEQUENTIAL:
        Q = new QueueSequential();
        break;
    case ONE_LOCK_GLOBAL_FQ:
        Q = new QueueSequentialLockGlobalFL();
        break;
    case TWO_LOCKS_GLOBAL_FQ:
        Q = new QueueSplitLockGlobalFL();
        break;
    
    default:
        printf("Type of queue is not supported");
        return 1;
    }
    

    Q->queue_init();

    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * n_threads);
    thread_stats_t *stats = (thread_stats_t*)aligned_alloc(64, sizeof(thread_stats_t) * n_threads);
    interval_t* thread_intervals = (interval_t*)malloc(sizeof(interval_t) * n_threads);

    int values_per_thread = enq_batch * repetitions;  // total enqueues per thread

    int start_value = 0;
    for (int i = 0; i < n_threads; i++) {
        thread_intervals[i].start = start_value;
        thread_intervals[i].end   = start_value + values_per_thread; // exclusive
        start_value += values_per_thread;
    }

    for (int i = 0; i < n_threads; i++) {
        stats[i].enq_count = 0;
        stats[i].deq_count = 0;
        stats[i].failed_deq_count = 0;
    }

    uint64_t t0 = now_ns();

    for (int i = 0; i < n_threads; i++) {
        thread_arg_t *arg = (thread_arg_t*)malloc(sizeof(thread_arg_t));
        arg->Q = Q;
        arg->enq_batch = enq_batch;
        arg->deq_batch = deq_batch;
        arg->repetitions = repetitions;
        arg->interval = thread_intervals[i];
        arg->stats = &stats[i];

        pthread_create(&threads[i], NULL, worker, arg);
    }

    for (int i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);

    uint64_t t1 = now_ns();

    // ------------------ Print Results ---------------------

    unsigned long enq_total = 0;
    unsigned long deq_total = 0;
    unsigned long failed_total = 0;

    for (int i = 0; i < n_threads; i++) {
        enq_total += stats[i].enq_count;
        deq_total += stats[i].deq_count;
        failed_total += stats[i].failed_deq_count;
    }

    double sec = (t1 - t0) / 1e9;

    printf("\n==== Benchmark Results ====\n");
    printf("Threads: %d\n", n_threads);
    printf("Time: %.3f sec\n", sec);
    printf("Total Enqueue: %lu\n", enq_total);
    printf("Total Dequeue: %lu\n", deq_total);
    printf("Failed Dequeues: %lu\n", failed_total);
    printf("Throughput: %.2f M ops/s\n",
           (enq_total + deq_total) / sec / 1e6);

    printf("\n==== Queue Internal Counters ====\n");
    printf("Freelist pushes:   %lu\n", Q->stats.freelist_pushes);
    printf("Freelist pops:     %lu\n", Q->stats.freelist_pops);
    printf("Freelist max size: %lu\n", Q->stats.freelist_max_size);
    printf("Nodes malloc'ed:   %lu\n", Q->stats.malloc_count);
    printf("Nodes reused:      %lu\n", Q->stats.reused_count);

    if (enq_total != deq_total) {
        printf("ERROR: Mismatch! enq_total=%lu deq_total=%lu\n",
               enq_total, deq_total);
    } else {
        printf("OK: All enqueued values were dequeued exactly once.\n");
}


    Q->queue_destroy();
    delete Q;
    free(stats);
    free(threads);

    return 0;
}

