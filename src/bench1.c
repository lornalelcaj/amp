// bench.c - benchmark for the sequential freelist-reuse queue.
// Build: gcc -O2 -pthread bench.c queue_impl.c -o bench

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>
#include <getopt.h>
#include <stdint.h>

#include "queue_seq.h"

typedef struct {
    queue Q;
    int enq_batch;
    int deq_batch;
    int repetitions;
    atomic_ulong *ops_counter;
} thread_arg_t;

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void *worker(void *arg_) {
    thread_arg_t *arg = arg_;
    value_t tmp;

    for (int rep = 0; rep < arg->repetitions; rep++) {
        // enqueue batch
        for (int i = 0; i < arg->enq_batch; i++)
            enq(i, arg->Q);

        // dequeue batch
        for (int i = 0; i < arg->deq_batch; i++) {
            deq(&tmp, arg->Q);
        }

        atomic_fetch_add(arg->ops_counter, arg->enq_batch + arg->deq_batch);
    }
    return NULL;
}

void *monitor_thread(void *arg_) {
    atomic_ulong *ops = arg_;
    unsigned long last = 0;

    while (1) {
        sleep(1);
        unsigned long cur = atomic_load(ops);
        printf("Throughput: %lu ops/sec\n", cur - last);
        last = cur;
    }
    return NULL;
}

int main(int argc, char **argv) {
    int n_threads = 4;
    int repetitions = 1000000;
    int interval_sec = 1;
    int enq_batch = 10;
    int deq_batch = 10;

    int opt;
    while ((opt = getopt(argc, argv, "t:r:i:E:D:")) != -1) {
        switch(opt) {
            case 't': n_threads  = atoi(optarg); break;
            case 'r': repetitions = atoi(optarg); break;
            case 'i': interval_sec = atoi(optarg); break;
            case 'E': enq_batch = atoi(optarg); break;
            case 'D': deq_batch = atoi(optarg); break;
            default:
                fprintf(stderr,
                    "Usage: %s [-t threads] [-r reps] [-i interval] [-E enq_batch] [-D deq_batch]\n",
                    argv[0]);
                exit(1);
        }
    }

    queue Q = malloc(sizeof(queue_t));
    queue_init(Q);

    pthread_t *threads = malloc(sizeof(pthread_t) * n_threads);
    pthread_t mon;

    atomic_ulong ops_counter = 0;

    // start monitor thread
    pthread_create(&mon, NULL, monitor_thread, &ops_counter);

    // start workers
    for (int i = 0; i < n_threads; i++) {
        thread_arg_t *arg = malloc(sizeof(thread_arg_t));
        arg->Q = Q;
        arg->enq_batch = enq_batch;
        arg->deq_batch = deq_batch;
        arg->repetitions = repetitions;
        arg->ops_counter = &ops_counter;

        pthread_create(&threads[i], NULL, worker, arg);
    }

    // wait for workers to finish
    for (int i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);

    printf("Final total ops: %lu\n", atomic_load(&ops_counter));

    // cleanup
    queue_destroy(Q);
    free(Q);
    free(threads);

    return 0;
}
