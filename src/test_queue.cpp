// test for ex2

// test_queue.cpp - Complete test program for thread-local queue
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <assert.h>
#include <getopt.h>
#include "queue_seq.h"
#include "queue_seq_lock_global_FL.h"
#include "queue_split_lock_global_FL.h"
#include "queue_lock_free_local_FL.h"
#include "thread_stats.h"
#include "queue_seq_lock_registry_FL.h"
#include "two_lock_queue.h"

// Test 1: Basic sequential functionality
void test_sequential(IQueue& queue) {
    printf("\n=== Test 1: Basic Sequential Operations ===\n");
    queue.thread_prepare();
    value_t val;
    
    // Test empty queue
    assert(queue.deq(&val) == 0);
    printf("+ Empty queue dequeue returns 0\n");
    
    // Test enqueue and dequeue
    queue.enq(10);
    queue.enq(20);
    queue.enq(30);
    printf("+ Enqueued 10, 20, 30\n");
    
    assert(queue.deq(&val) == 1 && val == 10);
    assert(queue.deq(&val) == 1 && val == 20);
    assert(queue.deq(&val) == 1 && val == 30);
    printf("+ Dequeued 10, 20, 30 in correct order (FIFO)\n");
    
    assert(queue.deq(&val) == 0);
    printf("+ Queue empty after all dequeues\n");

    queue.thread_cleanup();
    printf("Test 1 PASSED\n");
}

// Test 2: Concurrent enqueue/dequeue operations
void test_concurrent_basic(IQueue& queue) {
    printf("\n=== Test 2: Concurrent Enqueue/Dequeue ===\n");
    
    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 10000;
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        queue.thread_prepare();

        // Each thread enqueues its own values
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            queue.enq(tid * (OPS_PER_THREAD * 10) + i);
        }

        queue.thread_cleanup();
    }
    
    printf("+ %d threads each enqueued %d items\n", NUM_THREADS, OPS_PER_THREAD);
    
    // Dequeue all items
    int count = 0;
    value_t val;
    while (queue.deq(&val) == 1) {
        count++;
    }
    
    assert(count == NUM_THREADS * OPS_PER_THREAD);
    printf("+ Dequeued all %d items successfully\n", count);
    
    printf("Test 2 PASSED\n");
}

// Test 3: Producer-Consumer pattern
void test_producer_consumer(IQueue& queue) {
    printf("\n=== Test 3: Producer-Consumer Pattern ===\n");

    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int ITEMS_PER_PRODUCER = 10000;
    const size_t MAX_NUM_CONSECUTIVE_MISSES = 1e10;
    
    int consumed_count = 0;
    
    #pragma omp parallel num_threads(NUM_PRODUCERS + NUM_CONSUMERS)
    {
        int tid = omp_get_thread_num();
        queue.thread_prepare();
        
        if (tid < NUM_PRODUCERS) {
            // Producer threads
            for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
                queue.enq(tid * 100000 + i);
            }
            printf("  Producer %d finished\n", tid);
        } else {
            // Consumer threads
            value_t val;
            int local_count = 0;
            
            // Keep trying to dequeue
            for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
                size_t num_misses = 0;
                while (queue.deq(&val) == 0) {
                    // Spin until something is available
                    num_misses++;
                    // give up after a certain number of consecutive misses, prevents deadlock in case of missing elements
                    if (num_misses > MAX_NUM_CONSECUTIVE_MISSES) {
                        printf("  Consumer %d gave up\n", tid);
                        assert(false);
                    } 
                }
                local_count++;
            }
            
            #pragma omp atomic
            consumed_count += local_count;
            
            printf("  Consumer %d consumed %d items\n", tid, local_count);
        }
        queue.thread_cleanup();
    }
    
    printf("+ Total consumed: %d items\n", consumed_count);
    assert(consumed_count == NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    
    printf("Test 3 PASSED\n");
}

// Test 4: Freelist reuse verification
void test_freelist_reuse(IQueue& queue) {
    printf("\n=== Test 4: Freelist Reuse ===\n");
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 1000;
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        queue.thread_prepare();
        value_t val;
        
        // Each thread does many enq/deq cycles
        for (int i = 0; i < ITERATIONS; i++) {
            queue.enq(i);
            queue.deq(&val);
        }
        queue.thread_cleanup();
    }
    
    // Check statistics
    thread_stats tqs = queue.getStats();
    printf("Statistics:\n");
    printf("  Total malloc calls: %lu\n", tqs.malloc_count);
    printf("  Total reused nodes: %lu\n", tqs.reused_count);
    printf("  Total freelist pushes: %lu\n", tqs.freelist_pushes);
    printf("  Total freelist pops: %lu\n", tqs.freelist_pops);
    printf("  Max freelist size: %lu\n", tqs.freelist_max_size);
    
    // Freelist should be working (reuse should be much higher than malloc)
    assert(tqs.reused_count > tqs.malloc_count);
    printf("+ Freelist reuse rate: %.2f%%\n", 
           100.0 * tqs.reused_count / 
           (tqs.reused_count + tqs.malloc_count));
    
    printf("Test 4 PASSED\n");
}

// Test 5: Stress test with high contention
void test_stress(IQueue& queue) {
    printf("\n=== Test 5: Stress Test ===\n");
    
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 1000000;
    
    double start_time = omp_get_wtime();
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        queue.thread_prepare();
        value_t val;
        
        // Mix of enqueue and dequeue operations
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            if (i % 2 == 0) {
                queue.enq(tid * 1000000 + i);
            } else {
                queue.deq(&val);  // May fail if queue empty
            }
        }
        queue.thread_cleanup();
    }
    
    double end_time = omp_get_wtime();
    printf("+ Completed %d operations across %d threads\n", 
           NUM_THREADS * OPS_PER_THREAD, NUM_THREADS);
    printf("+ Time: %.3f seconds\n", end_time - start_time);
    printf("+ Throughput: %.2f M ops/s\n", 
           (NUM_THREADS * OPS_PER_THREAD) / 1e6 / (end_time - start_time) );
    
    printf("Test 5 PASSED\n");
}

// Test 6: Thread-local freelist verification
void test_thread_local_freelists(IQueue& queue) {
    printf("\n=== Test 6: Thread-Local Freelist Verification ===\n");
    
    const int NUM_THREADS = 4;
    const int ELEMENTS_PER_THREAD = 10;
    const int NUM_TOTAL_ELEMENTS = NUM_THREADS * ELEMENTS_PER_THREAD;
    
    std::atomic_int8_t signal = 0;

    printf("Each thread will deq %d elements.\n", ELEMENTS_PER_THREAD);
    printf("If freelists are truly thread-local, elements should be split between lists\n");

    for(int i = 0; i < NUM_TOTAL_ELEMENTS; i++){
        queue.enq(i);
    }

    printf("%d elements enqueueed.\n", NUM_TOTAL_ELEMENTS);
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        queue.thread_prepare();
        value_t val;
        
        // one thread after the other takes elements from the queue
        while (signal != tid) {
            /* spin till its the threads turn */
        }
        
        int successes = 0;
        for (size_t i = 0; i < ELEMENTS_PER_THREAD; i++) {   
            int val;
            successes += queue.deq(&val);
        }
        printf("  Thread %d dequeued %d/%d elements\n", tid, successes, ELEMENTS_PER_THREAD);
        assert(successes == ELEMENTS_PER_THREAD);

        signal++;

        while (signal != NUM_THREADS) {
            /* spin till all threads done */
        }
        printf("  Thread %d tls freelist_max_size = %lu\n", tid, tls_stats.freelist_max_size);

        queue.thread_cleanup();
        printf("  Thread %d completed\n", tid);
    }
    
    thread_stats tqs = queue.getStats();
    printf("+ Max freelist size: %lu (should be %d)\n", 
           tqs.freelist_max_size, ELEMENTS_PER_THREAD);
    
    // This is not using an assert since queuetypes without local freelists will always fail this test
    if (tqs.freelist_max_size == ELEMENTS_PER_THREAD)
        printf("Test 6 PASSED\n");
    else
        printf("Test 6 FAILED\n");
}

enum Queue_Type {
    SEQUENTIAL, 
    ONE_LOCK_LOCAL_FQ, 
    ONE_LOCK_GLOBAL_FQ, 
    TWO_LOCKS_LOCAL_FQ, 
    TWO_LOCKS_GLOBAL_FQ,
    LOCK_FREE 
};

IQueue* getNewQueue(Queue_Type t) {
    switch (t) {
    case SEQUENTIAL:
        return new QueueSequential();
    case ONE_LOCK_GLOBAL_FQ:
        return new QueueSequentialLockGlobalFL();
    case ONE_LOCK_LOCAL_FQ:
        return new QueueSequentialLockRegistryFL();
    case TWO_LOCKS_GLOBAL_FQ:
        return new QueueSplitLockGlobalFL();
    case TWO_LOCKS_LOCAL_FQ:
        return new TwoLockQueue();
    case LOCK_FREE:
        return new QueueLockFreeLocalFL();
    
    default:
        printf("Type of queue is not supported\n");
        exit(1);
    }
}

int main(int argc, char **argv) {
    Queue_Type queue_type = LOCK_FREE;
    int opt;
    size_t selected_test = 0;
    while ((opt = getopt(argc, argv, "Q:T:")) != -1) {
        switch(opt) {
            case 'Q': queue_type = Queue_Type(atoi(optarg)); break;
            case 'T': selected_test = atoi(optarg); break;
        }
    }

    constexpr int NUMBER_TESTS = 6;
    void (*test[NUMBER_TESTS]) (IQueue&) = {
        test_sequential, 
        test_concurrent_basic, 
        test_producer_consumer, 
        test_freelist_reuse,
        test_stress,
        test_thread_local_freelists
    };

    IQueue* Q;

    printf("\n Test Queue type: %d\n", queue_type);

    for (size_t i = 0; i < NUMBER_TESTS; i++) {
        if (selected_test != 0 && i + 1 != selected_test) continue;
        
        // get new queue, test it and cleanup for the next test
        Q = getNewQueue(queue_type);
        Q->queue_init();
        test[i](*Q);
        Q->queue_destroy();
        delete Q;

        if (queue_type == SEQUENTIAL) break;
    }
    return 0;
}
