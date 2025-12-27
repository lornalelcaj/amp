/*
bench.c - instrumented benchmark
Build: g++ -fopenmp \
   queue_benchmark.cpp \
   queue_seq.cpp \
   queue_seq_lock_global_FL.cpp \
   queue_split_lock_global_FL.cpp \
   queue_lock_free_local_FL.cpp \
   thread_stats_tls.cpp \
   -o queue_benchmark
*/

#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <getopt.h>
#include <assert.h>
#include <vector>

#include "queue_seq.h"
#include "queue_seq_lock_global_FL.h"
#include "queue_split_lock_global_FL.h"
#include "queue_lock_free_local_FL.h"
#include "thread_stats.h"
#include "thread_stats_tls.h"

// ------------------ Timing ---------------------

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}


// ------------------ Worker Thread ---------------------

void* worker(void *arg_) {
    thread_stats_t* ts = (thread_stats_t*)arg_;
    tls_stats = ts;
    value_t tmp;
    int val = ts->interval_start;
    
    ts->Q->thread_prepare();

    pthread_barrier_wait(ts->barrier);

    for (int rep = 0; rep < ts->repetitions; rep++) {

        for (int i = 0; i < ts->enq_batch; i++) {
            ts->Q->enq(val++);
            ts->enq_count++;
        }

        for (int i = 0; i < ts->deq_batch; i++) {
          if (ts->Q->deq(&tmp)) {
            ts->deq_count++;

            if (tmp < 0 || (size_t)tmp >= ts->total_values) {
              printf("ERROR: Invalid dequeued value %d\n", tmp);
            } else {
              ts->dequeued_values->push_back(tmp);
            }
          } else {
            ts->failed_deq_count++;
          }
        }
    }

    pthread_barrier_wait(ts->barrier);

    ts->Q->thread_cleanup();
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
    int max_number_error_messages = 20;
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
            case 'm': max_number_error_messages = atoi(optarg); break;
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
    thread_stats_t* stats = (thread_stats_t*)aligned_alloc(64, sizeof(thread_stats_t) * n_threads);

    int values_per_thread = enq_batch * repetitions;  // total enqueues per thread
    unsigned long total_values = values_per_thread * n_threads;
    int start_value = 0;
    
    // create threads
    for (int i = 0; i < n_threads; i++) {
      stats[i].thread_id = i;
      stats[i].n_threads = n_threads;
      stats[i].Q = Q;
      stats[i].enq_batch = enq_batch;
      stats[i].deq_batch = deq_batch;
      stats[i].repetitions = repetitions;
      stats[i].total_values = total_values;
      stats[i].barrier = &barrier;
      stats[i].interval_start = start_value;
      stats[i].interval_end = start_value + values_per_thread;
      stats[i].dequeued_values =
          new std::vector<value_t>(values_per_thread);

      pthread_create(&threads[i], NULL, worker, &stats[i]);
      start_value += values_per_thread;
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
    unsigned long fl_pushes = 0, fl_pops = 0, fl_max = 0;
    unsigned long mallocs = 0, reused = 0;
    std::vector<unsigned int> global_seen(total_values, 0);

    for (int i = 0; i < n_threads; i++) {
    
        fl_pushes += stats[i].freelist_pushes;
        fl_pops   += stats[i].freelist_pops;
        mallocs   += stats[i].malloc_count;
        reused    += stats[i].reused_count;
        fl_max = std::max(fl_max, stats[i].freelist_max_size);
    
        enq_total += stats[i].enq_count;
        deq_total += stats[i].deq_count;
        failed_total += stats[i].failed_deq_count;
        for (value_t v : *stats[i].dequeued_values) {
            global_seen[v]++;
        }
        delete(stats[i].dequeued_values);
    }
    // drain the queue if there are any remaining values
    value_t v;
    size_t remaining_nodes = 0;
    while(Q->deq(&v)) {
        global_seen[v]++;
        remaining_nodes++;
    }
    if (remaining_nodes > 0) {
        printf("Remaining queue elements: %lu\n", remaining_nodes);
    }
    deq_total += remaining_nodes;

    double sec = (t1 - t0) / 1e9;

    printf("\nQueue type: %d\n", queue_type);
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
    printf("Freelist pushes:   %lu\n", fl_pushes);
    printf("Freelist pops:     %lu\n", fl_pops);
    printf("Freelist max size: %lu\n", fl_max);
    printf("Nodes malloc'ed:   %lu\n", mallocs);
    printf("Nodes reused:      %lu\n", reused);



    if (enq_total != deq_total) {
      printf("ERROR: Mismatch! enq_total=%lu deq_total=%lu\n", enq_total, deq_total);
      int error_count = 0;
      for (unsigned long i = 0; i < total_values; i++) {
        if (global_seen[i] == 0) {
          if (error_count++ < max_number_error_messages) {
            printf("ERROR: missing value %lu\n", i);
          } else {
            printf("Further errors omitted...\n");
            break;
          }
        } else if (global_seen[i] > 1) {
          if (error_count++ < max_number_error_messages) {
            printf("ERROR: duplicate value %lu (%u times)\n", i, global_seen[i]);
          } else {
            printf("Further errors omitted...\n");
            break;
          }
        }
      }
    } else {
        printf("OK: All enqueued values were dequeued exactly once.\n");
    }


    Q->queue_destroy();
    delete Q;
    free(stats);
    free(threads);

    return 0;
}

