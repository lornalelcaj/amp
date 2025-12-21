// bench.c - instrumented benchmark
// Build: gcc -O2 -pthread bench.c queue_impl.c -o bench

#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>

#include "queue_seq.h"
#include "queue_seq_lock_global_FL.h"
#include "queue_split_lock_global_FL.h"
#include "queue_lock_free_local_FL.h"

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
    int thread_id;
    int n_threads;
    IQueue* Q;
    int enq_batch;
    int deq_batch;
    int repetitions;
    thread_stats_t* stats;
    pthread_barrier_t* barrier;
} thread_arg_t;

void* worker(void *arg_) {
    thread_arg_t *arg = (thread_arg_t*)arg_;
    value_t tmp;
    
    arg->Q->thread_prepare();

    pthread_barrier_wait(arg->barrier);

    for (int rep = 0; rep < arg->repetitions; rep++) {

        for (int i = 0; i < arg->enq_batch; i++) {
            arg->Q->enq(i * arg->n_threads + arg->thread_id);
            arg->stats->enq_count++;
        }

        for (int i = 0; i < arg->deq_batch; i++) {
            if (arg->Q->deq(&tmp))
                arg->stats->deq_count++;
            else
                arg->stats->failed_deq_count++;
        }
    }

    pthread_barrier_wait(arg->barrier);

    arg->Q->thread_cleanup();

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
    case LOCK_FREE:
        Q = new QueueLockFreeLocalFL();
        break;
    
    default:
        printf("Type of queue is not supported");
        return 1;
    }
    

    Q->queue_init();

    // barrier to sync all spawned threads and main
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, n_threads + 1); 

    // init threads
    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * n_threads);
    thread_stats_t *stats = (thread_stats_t*)aligned_alloc(64, sizeof(thread_stats_t) * n_threads);

    for (int i = 0; i < n_threads; i++) {
        stats[i].enq_count = 0;
        stats[i].deq_count = 0;
        stats[i].failed_deq_count = 0;
    }

    // create threads
    for (int i = 0; i < n_threads; i++) {
        thread_arg_t *arg = (thread_arg_t*)malloc(sizeof(thread_arg_t));
        arg->thread_id = i;
        arg->n_threads = n_threads;
        arg->Q = Q;
        arg->enq_batch = enq_batch;
        arg->deq_batch = deq_batch;
        arg->repetitions = repetitions;
        arg->stats = &stats[i];
        arg->barrier = &barrier;
        
        pthread_create(&threads[i], NULL, worker, arg);
    }

    // start experiment
    uint64_t t0 = now_ns();
    pthread_barrier_wait(&barrier);
    
    // end experiment
    pthread_barrier_wait(&barrier);
    uint64_t t1 = now_ns();

    for (int i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);

    pthread_barrier_destroy(&barrier);
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

    printf("\n Queue type: %d\n", queue_type);
    printf("\n==== Benchmark Results ====\n");
    printf("Threads: %d\n", n_threads);
    printf("Time: %.3f sec\n", sec);
    printf("Total Enqueue: %lu\n", enq_total);
    printf("Total Dequeue: %lu\n", deq_total);
    double failed_percent = ((long double)(failed_total)/deq_total) * 100;
    printf("Failed Dequeues: %lu (%.3f%%)\n", failed_total, failed_percent);
    printf("Throughput: %.2f M ops/s\n",
           (enq_total + deq_total) / sec / 1e6);

    printf("\n==== Queue Internal Counters ====\n");
    printf("Freelist pushes:   %lu\n", Q->stats.freelist_pushes);
    printf("Freelist pops:     %lu\n", Q->stats.freelist_pops);
    printf("Freelist max size: %lu\n", Q->stats.freelist_max_size);
    printf("Nodes malloc'ed:   %lu\n", Q->stats.malloc_count);
    printf("Nodes reused:      %lu\n", Q->stats.reused_count);

    Q->queue_destroy();
    delete Q;
    free(stats);
    free(threads);

    return 0;
}

