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

struct thread_arguments {
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
};

void* worker(void *arg_) {
    thread_arguments* args = (thread_arguments*)arg_;
    value_t tmp;
    int val = args->interval_start;
    
    args->Q->thread_prepare();

    pthread_barrier_wait(args->barrier);

    for (int rep = 0; rep < args->repetitions; rep++) {

        for (int i = 0; i < args->enq_batch; i++) {
            args->Q->enq(val++);
            tls_stats.enq_count++;
        }

        for (int i = 0; i < args->deq_batch; i++) {
          if (args->Q->deq(&tmp)) {
            tls_stats.deq_count++;

            if (tmp < 0 || (size_t)tmp >= args->total_values) {
              printf("ERROR: Invalid dequeued value %d\n", tmp);
            } else {
              tls_stats.dequeued_values.push_back(tmp);
            }
          } else {
            tls_stats.failed_deq_count++;
          }
        }
    }

    pthread_barrier_wait(args->barrier);

    args->Q->thread_cleanup();
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
    thread_arguments* thread_args = (thread_arguments*)malloc(sizeof(thread_arguments) * n_threads);

    int values_per_thread = enq_batch * repetitions;  // total enqueues per thread
    unsigned long total_values = values_per_thread * n_threads;
    int start_value = 0;
    
    // create threads
    for (int i = 0; i < n_threads; i++) {
      thread_args[i].thread_id = i;
      thread_args[i].n_threads = n_threads;

      thread_args[i].enq_batch = enq_batch;
      thread_args[i].deq_batch = deq_batch;
      thread_args[i].repetitions = repetitions;
      
      thread_args[i].interval_start = start_value;
      thread_args[i].interval_end = start_value + values_per_thread;
      thread_args[i].total_values = total_values;
      
      thread_args[i].barrier = &barrier;
      thread_args[i].Q = Q;
      
      pthread_create(&threads[i], NULL, worker, &thread_args[i]);
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

    thread_stats tqs = Q->getStats(); // total queue stats
    std::vector<unsigned int> global_seen(total_values, 0);
    for(auto& v: tqs.dequeued_values) {
      global_seen[v]++;
    }

    // drain the queue if there are any remaining values
    value_t v;
    size_t remaining_nodes = 0;
    while(Q->deq(&v)) {
        global_seen[v]++;
        remaining_nodes++;
    }

    double sec = (t1 - t0) / 1e9;

    printf("\nQueue type: %d\n", queue_type);
    printf("\n==== Benchmark Results ====\n");
    printf("Threads: %d\n", n_threads);
    printf("Time: %.3f sec\n", sec);
    printf("Total Enqueue: %lu\n", tqs.enq_count);
    printf("Total Dequeue: %lu\n", tqs.deq_count);
    double failed_percent = ((long double)(tqs.failed_deq_count)/tqs.deq_count) * 100;
    printf("Failed Dequeues: %lu (%.3f%%)\n", tqs.failed_deq_count, failed_percent);
    printf("Remaining queue elements: %lu\n", remaining_nodes);
    printf("Throughput: %.2f M ops/s\n",
           (tqs.enq_count + tqs.deq_count) / sec / 1e6);

    printf("\n==== Queue Internal Counters ====\n");
    printf("Freelist pushes:   %lu\n", tqs.freelist_pushes);
    printf("Freelist pops:     %lu\n", tqs.freelist_pops);
    printf("Freelist max size: %lu\n", tqs.freelist_max_size);
    printf("Nodes malloc'ed:   %lu\n", tqs.malloc_count);
    printf("Nodes reused:      %lu\n", tqs.reused_count);


    if (tqs.enq_count != tqs.deq_count + remaining_nodes) {
      printf("ERROR: Mismatch! enq_total=%lu deq_total=%lu\n", tqs.enq_count, tqs.deq_count);
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
        printf("OK: No enqueued values have been lost.\n");
    }


    Q->queue_destroy();
    delete Q;
    free(thread_args);
    free(threads);

    return 0;
}

