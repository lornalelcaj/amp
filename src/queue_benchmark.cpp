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
#include "queue_seq_lock_local_FL.h"
#include "queue_split_lock_global_FL.h"
#include "queue_split_lock_local_FL.h"
#include "queue_lock_free_local_FL.h"
#include "concurrent_bag.h"
#include "concurrent_bag_factory.h"
#include "thread_stats.h"
#include "thread_stats_tls.h"
#include "queue_benchmark_c_api.h"

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
  int max_enq_batches = 0;
  int interval_start = 0;
  int interval_end = 0;
  unsigned long total_values = 0;
  bool track_deq_values = false;

  uint64_t max_duration_ns = 0;
  
  IQueue* Q = nullptr;
  pthread_barrier_t* barrier = nullptr;

  // Padding to avoid false sharing
  char pad[64];
};

enum queue_types {
    SEQUENTIAL, 
    ONE_LOCK_LOCAL_FQ, 
    ONE_LOCK_GLOBAL_FQ, 
    TWO_LOCKS_LOCAL_FQ, 
    TWO_LOCKS_GLOBAL_FQ,
    LOCK_FREE,
    LOCK_FREE_BAG
};

IQueue* getNewQueue(queue_types t) {
    switch (t) {
    case SEQUENTIAL:
        return new QueueSequential();
    case ONE_LOCK_GLOBAL_FQ:
        return new QueueSequentialLockGlobalFL();
    case ONE_LOCK_LOCAL_FQ:
        return new QueueSequentialLockLocalFL();
    case TWO_LOCKS_GLOBAL_FQ:
        return new QueueSplitLockGlobalFL();
    case TWO_LOCKS_LOCAL_FQ:
        return new QueueSplitLockLocalFL();
    case LOCK_FREE:
        return new QueueLockFreeLocalFL();
    case LOCK_FREE_BAG:
    printf("Creating Concurrent Bag of Lock-Free Queues\n");
        return make_concurrent_bag_lockfree_localfl_omp();
    
    default:
        printf("Type of queue is not supported\n");
        return NULL;
    }
}


void* worker(void *arg_) {
    thread_arguments* args = (thread_arguments*)arg_;
    value_t tmp;
    uint64_t start_ts = 0;
    uint64_t end_ts = 0;
    int val = args->interval_start;
    
    args->Q->thread_prepare();

    pthread_barrier_wait(args->barrier);
    start_ts = now_ns();

    for (int rep = 0; true; rep++) {
        end_ts = now_ns();
        if (end_ts > start_ts + args->max_duration_ns) break;

        if (args->enq_batch > 0 && rep >= args->max_enq_batches) break;

        for (int i = 0; i < args->enq_batch; i++) {
            args->Q->enq(val++);
            tls_stats.enq_count++;
            
            end_ts = now_ns();
            if (end_ts > start_ts + args->max_duration_ns) break;
        }
        
        if (end_ts > start_ts + args->max_duration_ns) break;

        for (int i = 0; i < args->deq_batch; i++) {
            tls_stats.deq_count++;
            if (args->Q->deq(&tmp)) {
                if (tmp < 0 || (size_t)tmp >= args->total_values) {
                    printf("ERROR: Invalid dequeued value %d\n", tmp);
                } else {
                    if (args->track_deq_values) {
                        tls_stats.dequeued_values.push_back(tmp);
                    }
                }
            } else {
                tls_stats.failed_deq_count++;
            }
            end_ts = now_ns();
            if (end_ts > start_ts + args->max_duration_ns) break;
            
        }
    }

    pthread_barrier_wait(args->barrier);
    tls_stats.cummulative_time_ns = end_ts - start_ts;
    args->Q->thread_cleanup();
    return NULL;
}


void test_enq_deq_consistency(
    const thread_stats_t& tqs, 
    const std::vector<unsigned int>& global_seen, 
    size_t remaining_nodes, size_t total_values,
    int max_number_error_messages
) {
    printf("\n==== Consistency Test ====.\n");
    printf("Remaining queue elements: %lu\n", remaining_nodes);
    if (tqs.enq_count != tqs.deq_count - tqs.failed_deq_count + remaining_nodes) {
      printf("ERROR: Mismatch! enq_total=%lu != deq_total=%lu + remaining=%lu\n", tqs.enq_count, tqs.deq_count, remaining_nodes);
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
}

void print_benchmark_results(queue_types queue_type, int n_threads, thread_stats &tqs) {
    printf("\nQueue type: %d\n", queue_type);
    printf("\n==== Benchmark Results ====\n");
    printf("Threads: %d\n", n_threads);
    double avg_duration = (tqs.cummulative_time_ns / 1e9) / n_threads;
    printf("Average Time: %.3f sec\n", avg_duration);
    printf("Total Enqueue: %lu\n", tqs.enq_count);
    printf("Total Dequeue: %lu\n", tqs.deq_count);
    double failed_percent = ((long double)(tqs.failed_deq_count) / tqs.deq_count) * 100;
    printf("Failed Dequeues: %lu (%.3f%%)\n", tqs.failed_deq_count, failed_percent);
    printf("Throughput: %.2f M ops/s\n", ((tqs.enq_count + tqs.deq_count) / 1e6) / avg_duration);

    printf("\n==== Queue Internal Counters ====.\n");
    printf("Freelist pushes:   %lu\n", tqs.freelist_pushes);
    printf("Freelist pops:     %lu\n", tqs.freelist_pops);
    printf("Freelist max size: %lu\n", tqs.freelist_max_size);
    printf("Nodes malloc'ed:   %lu\n", tqs.malloc_count);
    printf("Nodes reused:      %lu\n", tqs.reused_count);

    unsigned long total_CAS = tqs.failed_CAS_ops + tqs.successful_CAS_ops;
    double failed_CAS_percent = ((long double)(tqs.failed_CAS_ops) / total_CAS) * 100;
    printf("Total CAS ops:     %lu\n", total_CAS);
    printf("Failed CAS ops:    %lu (%.3f%%)\n", tqs.failed_CAS_ops, failed_CAS_percent);
}

void execute_experiment(int n_threads, pthread_t *threads, thread_arguments *args) {
    // start all other threads
    for (int i = 1; i < n_threads; i++) {
        int ret = pthread_create(&threads[i], NULL, worker, &args[i]);
        if (ret)
            printf("ERROR: creating thread %d, pthread error status:%d\n", i, ret);
    }

    // run main thread workload
    worker(&args[0]);

    // join other threads
    for (int i = 1; i < n_threads; i++)
        pthread_join(threads[i], NULL);
}

//This is for python
CThreadStats run_queue_benchmark(
    int n_threads,
    int max_enq_batches,
    const int* enq_batches, //must be length n_threads
    const int* deq_batches, //must be length n_threads
    int queue_type,
    double max_duration_sec,
    bool check_dequeued_values
) {
    
    uint64_t max_duration_ns = (uint64_t)(max_duration_sec * 1e9);

    IQueue* Q = getNewQueue(queue_types(queue_type));
    if (Q == NULL) return {};

    Q->queue_init();

    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, n_threads);


    // init threads
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * n_threads);
    thread_arguments* args = (thread_arguments*)malloc(sizeof(thread_arguments) * n_threads);

    unsigned long total_values = 0;
    for (int i = 0; i < n_threads; i++) {
        total_values += enq_batches[i] * max_enq_batches;
    }

    int start_value = 0;
    for (int i = 0; i < n_threads; i++) {
        int values_per_thread = enq_batches[i] * max_enq_batches;
        args[i].thread_id = i;
        args[i].n_threads = n_threads;
        args[i].enq_batch = enq_batches[i];
        args[i].deq_batch = deq_batches[i];
        args[i].max_enq_batches = max_enq_batches;
        args[i].interval_start = start_value;
        args[i].interval_end = start_value + values_per_thread;
        args[i].total_values = total_values;
        args[i].max_duration_ns = max_duration_ns;
        args[i].barrier = &barrier;
        args[i].Q = Q;
        args[i].track_deq_values = check_dequeued_values;
        start_value += values_per_thread;
    }

    // run experiment
    execute_experiment(n_threads, threads, args);

    // evaluate results
    thread_stats tqs = Q->getStats();
    print_benchmark_results(queue_types(queue_type), n_threads, tqs);

    if (check_dequeued_values) {
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
        test_enq_deq_consistency(tqs, global_seen, remaining_nodes, total_values, 20);
    }


    CThreadStats out{};
    out.enq_count = tqs.enq_count;
    out.deq_count = tqs.deq_count;
    out.failed_deq_count = tqs.failed_deq_count;
    out.duration_ns = tqs.cummulative_time_ns/n_threads;

    out.freelist_pushes = tqs.freelist_pushes;
    out.freelist_pops = tqs.freelist_pops;
    out.freelist_max_size = tqs.freelist_max_size;
    out.malloc_count = tqs.malloc_count;
    out.reused_count = tqs.reused_count;

    out.successful_CAS_ops = tqs.successful_CAS_ops;
    out.failed_CAS_ops = tqs.failed_CAS_ops;

    pthread_barrier_destroy(&barrier);
    Q->queue_destroy();
    delete Q;
    free(args);
    free(threads);

    return out;
}

// ------------------ Benchmark Driver ---------------------
int main(int argc, char **argv) {
    int n_threads = 4;
    int repetitions = 1000000;
    int enq_batch = 10;
    int deq_batch = 10;
    int max_number_error_messages = 20;
    double max_duration_sec = 5.0; // default: 1 second
    uint64_t max_duration_ns = 0;
    queue_types queue_type = SEQUENTIAL;
    
    int opt;
    while ((opt = getopt(argc, argv, "t:r:E:D:Q:T:")) != -1) {
        switch(opt) {
            case 't': n_threads = atoi(optarg); break;
            case 'r': repetitions = atoi(optarg); break;
            case 'E': enq_batch = atoi(optarg); break;
            case 'D': deq_batch = atoi(optarg); break;
            case 'Q': queue_type = queue_types(atoi(optarg)); break;
            case 'm': max_number_error_messages = atoi(optarg); break;
            case 'T': max_duration_sec = atof(optarg); break;
        }
    }
    max_duration_ns = (uint64_t)(max_duration_sec * 1e9);

    IQueue* Q = getNewQueue(queue_types(queue_type));
    if (Q == NULL) return 1;

    Q->queue_init();

    // barrier to sync all spawned threads and main
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, n_threads); 

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
      thread_args[i].max_enq_batches = repetitions;
      
      thread_args[i].interval_start = start_value;
      thread_args[i].interval_end = start_value + values_per_thread;
      thread_args[i].total_values = total_values;
      
      thread_args[i].max_duration_ns = max_duration_ns;
      
      thread_args[i].barrier = &barrier;
      thread_args[i].Q = Q;
      start_value += values_per_thread;
    }

    // run experiment
    execute_experiment(n_threads, threads, thread_args);

    // ------------------ Results ---------------------
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
    
    print_benchmark_results(queue_type, n_threads, tqs);
    test_enq_deq_consistency(tqs, global_seen, remaining_nodes, total_values, max_number_error_messages);
    
    // cleanup
    pthread_barrier_destroy(&barrier);
    Q->queue_destroy();
    delete Q;
    free(thread_args);
    free(threads);

    return 0;
}