// bench.c - instrumented benchmark
// Build: gcc -O2 -pthread bench.c queue_impl.c -o bench

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>
#include <stdint.h>
#include <getopt.h>

#include "queue_split_lock.h"

// ------------------ Thread stats (avoid false sharing) --------------

typedef struct {
    unsigned long enq_count;
    unsigned long deq_count;
    unsigned long failed_deq_count;
    char pad[64];
} thread_stats_t;


// ------------------ Timing ---------------------

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}


// ------------------ Worker Thread ---------------------

typedef struct {
    queue Q;
    int enq_batch;
    int deq_batch;
    int repetitions;
    thread_stats_t *stats;
} thread_arg_t;

void *worker(void *arg_) {
    thread_arg_t *arg = arg_;
    value_t tmp;

    for (int rep = 0; rep < arg->repetitions; rep++) {

        for (int i = 0; i < arg->enq_batch; i++) {
            enq(i, arg->Q);
            arg->stats->enq_count++;
        }

        for (int i = 0; i < arg->deq_batch; i++) {
            if (deq(&tmp, arg->Q))
                arg->stats->deq_count++;
            else
                arg->stats->failed_deq_count++;
        }
    }
    free(arg_);
    return NULL;
}


// ------------------ Benchmark Driver ---------------------

int main(int argc, char **argv) {
    int n_threads = 4;
    int repetitions = 1000000;
    int enq_batch = 10;
    int deq_batch = 10;
    
    //TODO variable batch size
    
    int opt;
    while ((opt = getopt(argc, argv, "t:r:E:D:")) != -1) {
        switch(opt) {
            case 't': n_threads = atoi(optarg); break;
            case 'r': repetitions = atoi(optarg); break;
            case 'E': enq_batch = atoi(optarg); break;
            case 'D': deq_batch = atoi(optarg); break;
        }
    }

    queue Q = malloc(sizeof(queue_t));
    queue_init(Q);

    pthread_t *threads = malloc(sizeof(pthread_t) * n_threads);
    thread_stats_t *stats = aligned_alloc(64, sizeof(thread_stats_t) * n_threads);

    for (int i = 0; i < n_threads; i++) {
        stats[i].enq_count = 0;
        stats[i].deq_count = 0;
        stats[i].failed_deq_count = 0;
    }

    uint64_t t0 = now_ns();

    for (int i = 0; i < n_threads; i++) {
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        arg->Q = Q;
        arg->enq_batch = enq_batch;
        arg->deq_batch = deq_batch;
        arg->repetitions = repetitions;
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

    queue_destroy(Q);
    free(Q);
    free(stats);
    free(threads);

    return 0;
}

