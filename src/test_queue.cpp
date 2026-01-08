// test for ex2

// test_queue.cpp - Complete test program for thread-local queue
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <atomic>
#include <assert.h>
#include <getopt.h>
#include <vector>
#include <algorithm>

#include "queue_seq.h"
#include "queue_seq_lock_global_FL.h"
#include "queue_seq_lock_local_FL.h"

#include "queue_split_lock_global_FL.h"
#include "queue_split_lock_local_FL.h"

#include "queue_lock_free_local_FL.h"

#include "concurrent_bag.h"
#include "concurrent_bag_factory.h"

#include "thread_stats.h"
#include "thread_local_free_list.h"


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

// Test 7: Concurrent bag semantics verification (detailed)
void test_bag_semantics(IQueue& bag) {
    printf("\n=== Test 7: Bag Semantics (Detailed Unordered Correctness) ===\n");


    const int NUM_THREADS   = 8;
    const int PRODUCERS     = NUM_THREADS / 2;
    const int CONSUMERS     = NUM_THREADS - PRODUCERS;

    const int PHASEA_PER_THREAD = 4000;   // enqueue-only warmup
    const int PHASEB_PER_PROD   = 8000;   // mixed phase producer load

    const int PHASEA_TOTAL = NUM_THREADS * PHASEA_PER_THREAD;
    const int PHASEB_TOTAL = PRODUCERS * PHASEB_PER_PROD;
    const int TOTAL        = PHASEA_TOTAL + PHASEB_TOTAL;

    printf("Config: threads=%d (producers=%d consumers=%d)\n",
           NUM_THREADS, PRODUCERS, CONSUMERS);
    printf("Phase A: enqueue-only total=%d\n", PHASEA_TOTAL);
    printf("Phase B: mixed enq/deq, produced total=%d\n", PHASEB_TOTAL);
    printf("Expected total items ever produced=%d\n", TOTAL);

    // Global seen array to detect duplicates / missing values.
    // Use atomic byte flags so multiple consumers can mark concurrently.
    std::vector<std::atomic_uint8_t> seen(TOTAL);
    for (int i = 0; i < TOTAL; ++i) seen[i].store(0, std::memory_order_relaxed);

    std::atomic<int> dequeued_total{0};
    std::atomic<int> produced_phaseb{0};


    //  parallel enqueue only
    // Values range: [0, PHASEA_TOTAL)

    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        bag.thread_prepare();

        const int base = tid * PHASEA_PER_THREAD;
        for (int i = 0; i < PHASEA_PER_THREAD; ++i) {
            bag.enq(base + i);
        }

        bag.thread_cleanup();
    }

    printf("+ Phase A done: enqueued %d items\n", PHASEA_TOTAL);


    // Producers enqueue values in: [PHASEA_TOTAL, TOTAL)
    // Consumers dequeue concurrently and mark 'seen'

    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        bag.thread_prepare();

        if (tid < PRODUCERS) {
            // Producer thread tid
            const int prod_id = tid;
            const int base = PHASEA_TOTAL + prod_id * PHASEB_PER_PROD;

            for (int i = 0; i < PHASEB_PER_PROD; ++i) {
                const int val = base + i;
                bag.enq(val);
            }
            produced_phaseb.fetch_add(PHASEB_PER_PROD, std::memory_order_relaxed);
        } else {
            // Consumer threads
            value_t v;
            int local_success = 0;

            // Keep dequeuing until we believe all items have been produced and consumed.
            // We stop when (produced_phaseb == PHASEB_TOTAL) AND (dequeued_total >= TOTAL).
            // To reduce spinning when queue is temporarily empty, we just retry.
            while (true) {
                if (bag.deq(&v)) {
                    // Validate range
                    if (v < 0 || v >= TOTAL) {
                        printf("ERROR: out-of-range value dequeued: %d\n", v);
                        assert(false);
                    }

                    // Mark as seen once
                    uint8_t expected = 0;
                    if (!seen[v].compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
                        printf("ERROR: duplicate value dequeued: %d\n", v);
                        assert(false);
                    }

                    local_success++;
                    dequeued_total.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // No value right now. Check stopping condition.
                    const int prod = produced_phaseb.load(std::memory_order_relaxed);
                    const int deq  = dequeued_total.load(std::memory_order_relaxed);

                    if (prod == PHASEB_TOTAL && deq >= TOTAL) break;

                    // Tiny pause to reduce hot spinning 
                    #pragma omp flush
                }
            }

            // optional per-consumer output 
            // printf("  Consumer %d dequeued %d items\n", tid, local_success);
            (void)local_success;
        }

        bag.thread_cleanup();
    }

    printf("+ Phase B done: produced_phaseb=%d, dequeued_total=%d\n",
           produced_phaseb.load(), dequeued_total.load());


    bag.thread_prepare();
    value_t v;
    int drained = 0;
    while (bag.deq(&v)) {
        if (v < 0 || v >= TOTAL) {
            printf("ERROR: out-of-range value dequeued in final drain: %d\n", v);
            assert(false);
        }
        uint8_t expected = 0;
        if (!seen[v].compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
            printf("ERROR: duplicate value dequeued in final drain: %d\n", v);
            assert(false);
        }
        drained++;
        dequeued_total.fetch_add(1, std::memory_order_relaxed);
    }

    // Now the bag should be empty (strong check): a bunch of deq attempts must fail.
    const int EMPTY_TRIES = 10000;
    for (int i = 0; i < EMPTY_TRIES; ++i) {
        int ok = bag.deq(&v);
        if (ok) {
            printf("ERROR: bag reported non-empty after drain; got value %d\n", v);
            assert(false);
        }
    }
    bag.thread_cleanup();

    printf("+ Phase C done: drained=%d, empty-check tries=%d all failed\n", drained, EMPTY_TRIES);

    int missing = 0;
    for (int i = 0; i < TOTAL; ++i) {
        if (seen[i].load(std::memory_order_relaxed) != 1) missing++;
    }

    if (missing != 0) {
        printf("ERROR: missing %d values out of %d\n", missing, TOTAL);
        assert(false);
    }

    printf("+ All %d values were dequeued exactly once (no loss, no duplicates)\n", TOTAL);
    printf("Test 7 PASSED\n");
}




enum Queue_Type {
    SEQUENTIAL, 
    ONE_LOCK_LOCAL_FQ, 
    ONE_LOCK_GLOBAL_FQ, 
    TWO_LOCKS_LOCAL_FQ, 
    TWO_LOCKS_GLOBAL_FQ,
    LOCK_FREE,
    LOCK_FREE_BAG
};

IQueue* getNewQueue(Queue_Type t) {
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

    constexpr int NUMBER_TESTS = 7;
    void (*test[NUMBER_TESTS]) (IQueue&) = {
        test_sequential, 
        test_concurrent_basic, 
        test_producer_consumer, 
        test_freelist_reuse,
        test_stress,
        test_thread_local_freelists,
        test_bag_semantics
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
