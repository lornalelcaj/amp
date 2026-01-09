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
#include <atomic>

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
  int interval_start = 0;
  int interval_end = 0; // exclusive
  unsigned long total_values = 0;
  bool track_deq_values = false;
  bool print_info = false;

  uint64_t max_duration_ns = 0;
  
  IQueue* Q = nullptr;
  pthread_barrier_t* barrier = nullptr;
  std::atomic<size_t>* num_val_enqueued = nullptr;

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

IQueue* getNewQueue(queue_types t, int n_threads) {
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
    //printf("Creating Concurrent Bag of Lock-Free Queues\n");
        return make_concurrent_bag_lockfree_localfl(n_threads);
    
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
    unsigned long num_values_to_enq = args->interval_end - args->interval_start;
    const int MAX_DEQ_PAST_END = 10; // indicates how often a dequeueing thread will try before giving up for good
    int deq_past_end = 0;
    bool finished = false;
    
    args->Q->thread_prepare();

    pthread_barrier_wait(args->barrier);
    start_ts = now_ns();

    for (int rep = 0; true; rep++) {
        end_ts = now_ns();
        if (end_ts > start_ts + args->max_duration_ns) break;

        // enqueue
        if (finished) break;
        for (int i = 0; i < args->enq_batch; i++) {
            if (tls_stats.enq_count >= num_values_to_enq) {
                if (args->print_info) printf("INFO: Thread %d is done enqueueing %lu values\n", args->thread_id, num_values_to_enq);
                finished = true;
                // notify other threads that elements have been enqueued 
                args->num_val_enqueued->fetch_add(tls_stats.enq_count, std::memory_order_acq_rel);
                break;
            }

            args->Q->enq(val++);
            tls_stats.enq_count++;
            
            end_ts = now_ns();
            if (end_ts > start_ts + args->max_duration_ns) break;
        }
        // end enqueue

        end_ts = now_ns();
        if (end_ts > start_ts + args->max_duration_ns) break;

        // dequeue
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
                deq_past_end = 0;
            } else {
                tls_stats.failed_deq_count++;
                // check if any more values will be enqueued, if no, try a few more times and then give up
                size_t num_enq = args->num_val_enqueued->load(std::memory_order_acquire);
                if (num_enq >= args->total_values) deq_past_end++;
            }
            end_ts = now_ns();
            if (end_ts > start_ts + args->max_duration_ns) break;
        }
        // end dequeue

        // thread gives up once it thinks that all possible values have been dequeued
        if (deq_past_end > MAX_DEQ_PAST_END) {
            if (args->print_info) printf("INFO: Thread %d gives up\n", args->thread_id);
            break;
        }
    }

    pthread_barrier_wait(args->barrier);
    tls_stats.cummulative_time_ns = end_ts - start_ts;
    args->Q->thread_cleanup();
    return NULL;
}

/**
 * tests if all enqueued values have been dequeued
 * returns true if any errors have been found
 */
bool test_enq_deq_consistency(
    const thread_stats_t& tqs, 
    const std::vector<unsigned int>& global_seen, 
    size_t remaining_nodes, size_t total_values,
    int max_number_error_messages
) {
    printf("==== Consistency Test ====.\n");
    //printf("Remaining queue elements: %lu\n", remaining_nodes);
    unsigned long successfull_deq = tqs.deq_count - tqs.failed_deq_count;
    if (tqs.enq_count != successfull_deq + remaining_nodes) {
        printf("ERROR: Mismatch! enq_total=%lu != successfull_deq=%lu + remaining=%lu\n", tqs.enq_count, successfull_deq, remaining_nodes);
        int error_count = 0;
        int error_count_missing = 0;
        int error_count_duplicate = 0;
        for (unsigned long i = 0; i < total_values; i++) {
            if (global_seen[i] == 0) {
                if (error_count++ < max_number_error_messages) {
                    printf("ERROR: missing value %lu\n", i);
                } 
                error_count_missing++;
            } else if (global_seen[i] > 1) {
                if (error_count++ < max_number_error_messages) {
                    printf("ERROR: duplicate value %lu (%u times)\n", i, global_seen[i]);
                } 
                error_count_duplicate++;
            }
        }
        if (error_count > max_number_error_messages) {
            printf("%d further errors omitted...\n", error_count - max_number_error_messages);
            printf("Summary: %d missing, %d duplicates\n", error_count_missing, error_count_duplicate);
        }
    } else {
        printf("OK: No enqueued values have been lost.\n");
        printf("\n");
        return 0;
    }
    printf("\n");
    return 1;
}

void print_benchmark_results(queue_types queue_type, int n_threads, thread_stats &tqs) {
    printf("==== Benchmark Results ====\n");
    printf("Queue type: %d\n", queue_type);
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
    printf("\n");
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

/**
 * prepares an array of thread arguments based on the inpus specification
 */
thread_arguments* make_thread_args(
    int n_threads,
    IQueue* Q,
    unsigned long total_values, 
    const int* enq_batches, 
    const int* deq_batches, 
    uint64_t max_duration_ns,
    pthread_barrier_t& barrier,
    std::atomic<size_t>& num_val_enqueued,
    bool check_dequeued_values,
    bool print_info
){
    thread_arguments* args = (thread_arguments*)malloc(sizeof(thread_arguments) * n_threads);
    int n_enq_threads = 0;
    for (int i = 0; i < n_threads; i++) {
        n_enq_threads += (enq_batches[i] > 0);
    }

    int values_per_thread = 0;
    int excess_vals = 0; // < n_enq_threads
    if (n_enq_threads > 0) {
        values_per_thread = total_values / n_enq_threads;
        excess_vals = total_values - values_per_thread * n_enq_threads;
    } else {
        total_values = 0;
    }

    unsigned long start_value = 0;
    for (int i = 0, enq_i = 0; i < n_threads; i++) {
        int values_for_thread = values_per_thread;
        // add one more element to the first few enqueueing threads
        if (enq_batches[i] && i < excess_vals) {
            values_for_thread += 1;
            enq_i++;
        }
        args[i].thread_id = i;
        args[i].n_threads = n_threads;
        args[i].enq_batch = enq_batches[i];
        args[i].deq_batch = deq_batches[i];
        args[i].interval_start = start_value;
        args[i].interval_end = start_value + values_for_thread;
        args[i].total_values = total_values;
        args[i].max_duration_ns = max_duration_ns;
        args[i].barrier = &barrier;
        args[i].Q = Q;
        args[i].track_deq_values = check_dequeued_values;
        args[i].num_val_enqueued = &num_val_enqueued;
        args[i].print_info = print_info;
        start_value += values_for_thread;
    }

    if (print_info) printf("INFO: total values: %lu, values assigned: %lu\n", total_values, start_value);

    return args;
}


thread_stats _run_queue_benchmark(
    int n_threads,
    unsigned long max_enq_values,
    const int* enq_batches, //must be length n_threads
    const int* deq_batches, //must be length n_threads
    int queue_type,
    double max_duration_sec,
    bool check_dequeued_values,
    int max_number_error_messages,
    bool print_results,
    bool print_info
) {
    
    uint64_t max_duration_ns = (uint64_t)(max_duration_sec * 1e9);

    IQueue* Q = getNewQueue(queue_types(queue_type), n_threads);
    if (Q == NULL) return {};

    Q->queue_init();
    
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, n_threads);
    
    
    // init threads
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * n_threads);
    
    std::atomic<size_t> num_val_enqueued; // tracks how many values have been enqueued across all threads
    atomic_init(&num_val_enqueued, 0); 
    
    unsigned long total_values = max_enq_values;
    
    thread_arguments* args = make_thread_args(
        n_threads,
        Q,
        total_values, 
        enq_batches, 
        deq_batches, 
        max_duration_ns,
        barrier,
        num_val_enqueued,
        check_dequeued_values,
        print_info
    );

    // run experiment
    execute_experiment(n_threads, threads, args);

    // evaluate results
    thread_stats tqs = Q->getStats();
    if (print_results)
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
        bool ret = test_enq_deq_consistency(tqs, global_seen, remaining_nodes, total_values, max_number_error_messages);
        if (!print_results && ret)
            print_benchmark_results(queue_types(queue_type), n_threads, tqs);
    }

    pthread_barrier_destroy(&barrier);
    Q->queue_destroy();
    delete Q;
    free(args);
    free(threads);

    return tqs;
}


//This is for python
CThreadStats run_queue_benchmark(
    int n_threads,
    unsigned long max_enq_values,
    const int* enq_batches, //must be length n_threads
    const int* deq_batches, //must be length n_threads
    int queue_type,
    double max_duration_sec,
    bool check_dequeued_values,
    bool print_results,
    bool print_info
) {
    thread_stats tqs = _run_queue_benchmark(
        n_threads,
        max_enq_values,
        enq_batches,
        deq_batches,
        queue_type,
        max_duration_sec,
        check_dequeued_values,
        20,
        print_results,
        print_info
    );

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
    return out;
}


// ------------------ Benchmark Driver ---------------------
int main(int argc, char **argv) {
    unsigned int n_threads = 4;
    int max_enq_values = 1000000;
    int enq_batch = 10;
    int deq_batch = 10;
    int max_number_error_messages = 20;
    double max_duration_sec = 5.0; // seconds
    queue_types queue_type = SEQUENTIAL;
    bool check_dequeued_values = true;
    bool print_results = true;
    bool print_info = true;
    
    int opt;
    while ((opt = getopt(argc, argv, "t:v:E:D:Q:T:")) != -1) {
        switch(opt) {
            case 't': n_threads = atoi(optarg); break;
            case 'v': max_enq_values = atoi(optarg); break;
            case 'E': enq_batch = atoi(optarg); break;
            case 'D': deq_batch = atoi(optarg); break;
            case 'Q': queue_type = queue_types(atoi(optarg)); break;
            case 'm': max_number_error_messages = atoi(optarg); break;
            case 'T': max_duration_sec = atof(optarg); break;
        }
    }

    int* enq_batches = (int*)malloc(sizeof(int) * n_threads); 
    int* deq_batches = (int*)malloc(sizeof(int) * n_threads); 
    
    for (size_t i = 0; i < n_threads; i++) {
        enq_batches[i] = enq_batch;
        deq_batches[i] = deq_batch;
    }

    _run_queue_benchmark(
        n_threads,
        max_enq_values,
        enq_batches,
        deq_batches,
        queue_type,
        max_duration_sec,
        check_dequeued_values,
        max_number_error_messages,
        print_results,
        print_info
    );

    free(enq_batches);
    free(deq_batches);
    return 0;
}