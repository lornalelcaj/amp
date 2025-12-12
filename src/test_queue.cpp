// test for ex2

// test_queue.cpp - Complete test program for thread-local queue
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <assert.h>
#include "queue_seq_lock_local_FL.h"

// Test 1: Basic sequential functionality
void test_sequential() {
    printf("\n=== Test 1: Basic Sequential Operations ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    value_t val;
    
    // Test empty queue
    assert(queue.deq(&val) == 0);
    printf("✓ Empty queue dequeue returns 0\n");
    
    // Test enqueue and dequeue
    queue.enq(10);
    queue.enq(20);
    queue.enq(30);
    printf("✓ Enqueued 10, 20, 30\n");
    
    assert(queue.deq(&val) == 1 && val == 10);
    assert(queue.deq(&val) == 1 && val == 20);
    assert(queue.deq(&val) == 1 && val == 30);
    printf("✓ Dequeued 10, 20, 30 in correct order (FIFO)\n");
    
    assert(queue.deq(&val) == 0);
    printf("✓ Queue empty after all dequeues\n");
    
    queue.queue_destroy();
    printf("Test 1 PASSED\n");
}

// Test 2: Concurrent enqueue/dequeue operations
void test_concurrent_basic() {
    printf("\n=== Test 2: Concurrent Enqueue/Dequeue ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 1000;
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        
        // Each thread enqueues its own values
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            queue.enq(tid * 10000 + i);
        }
    }
    
    printf("✓ %d threads each enqueued %d items\n", NUM_THREADS, OPS_PER_THREAD);
    
    // Dequeue all items
    int count = 0;
    value_t val;
    while (queue.deq(&val) == 1) {
        count++;
    }
    
    assert(count == NUM_THREADS * OPS_PER_THREAD);
    printf("✓ Dequeued all %d items successfully\n", count);
    
    queue.queue_destroy();
    printf("Test 2 PASSED\n");
}

// Test 3: Producer-Consumer pattern
void test_producer_consumer() {
    printf("\n=== Test 3: Producer-Consumer Pattern ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    const int NUM_PRODUCERS = 2;
    const int NUM_CONSUMERS = 2;
    const int ITEMS_PER_PRODUCER = 5000;
    
    int consumed_count = 0;
    
    #pragma omp parallel num_threads(NUM_PRODUCERS + NUM_CONSUMERS)
    {
        int tid = omp_get_thread_num();
        
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
                while (queue.deq(&val) == 0) {
                    // Spin until something is available
                }
                local_count++;
            }
            
            #pragma omp atomic
            consumed_count += local_count;
            
            printf("  Consumer %d consumed %d items\n", tid, local_count);
        }
    }
    
    assert(consumed_count == NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    printf("✓ Total consumed: %d items\n", consumed_count);
    
    queue.queue_destroy();
    printf("Test 3 PASSED\n");
}

// Test 4: Freelist reuse verification
void test_freelist_reuse() {
    printf("\n=== Test 4: Freelist Reuse ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 1000;
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        value_t val;
        
        // Each thread does many enq/deq cycles
        for (int i = 0; i < ITERATIONS; i++) {
            queue.enq(i);
            queue.deq(&val);
        }
    }
    
    queue.queue_destroy();
    
    // Check statistics
    printf("Statistics:\n");
    printf("  Total malloc calls: %lu\n", queue.stats.malloc_count);
    printf("  Total reused nodes: %lu\n", queue.stats.reused_count);
    printf("  Total freelist pushes: %lu\n", queue.stats.freelist_pushes);
    printf("  Total freelist pops: %lu\n", queue.stats.freelist_pops);
    printf("  Max freelist size: %lu\n", queue.stats.freelist_max_size);
    
    // Freelist should be working (reuse should be much higher than malloc)
    assert(queue.stats.reused_count > queue.stats.malloc_count);
    printf("✓ Freelist reuse rate: %.2f%%\n", 
           100.0 * queue.stats.reused_count / 
           (queue.stats.reused_count + queue.stats.malloc_count));
    
    printf("Test 4 PASSED\n");
}

// Test 5: Stress test with high contention
void test_stress() {
    printf("\n=== Test 5: Stress Test ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 10000;
    
    double start_time = omp_get_wtime();
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        value_t val;
        
        // Mix of enqueue and dequeue operations
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            if (i % 2 == 0) {
                queue.enq(tid * 1000000 + i);
            } else {
                queue.deq(&val);  // May fail if queue empty
            }
        }
    }
    
    double end_time = omp_get_wtime();
    
    printf("✓ Completed %d operations across %d threads\n", 
           NUM_THREADS * OPS_PER_THREAD, NUM_THREADS);
    printf("✓ Time: %.3f seconds\n", end_time - start_time);
    printf("✓ Throughput: %.0f ops/sec\n", 
           (NUM_THREADS * OPS_PER_THREAD) / (end_time - start_time));
    
    queue.queue_destroy();
    printf("Test 5 PASSED\n");
}

// Test 6: Thread-local freelist verification
void test_thread_local_freelists() {
    printf("\n=== Test 6: Thread-Local Freelist Verification ===\n");
    QueueSequentialLockThreadLocalFL queue;
    queue.queue_init();
    
    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 100;
    
    printf("Each thread will do %d enq/deq pairs\n", OPS_PER_THREAD);
    printf("If freelists are truly thread-local, each thread should reuse its own nodes\n");
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        value_t val;
        
        // Each thread does enq/deq cycles
        for (int i = 0; i < OPS_PER_THREAD; i++) {
            queue.enq(tid * 1000 + i);
            queue.deq(&val);
        }
        
        printf("  Thread %d completed\n", tid);
    }
    
    queue.queue_destroy();
    
    printf("✓ Malloc count: %lu (should be close to NUM_THREADS)\n", 
           queue.stats.malloc_count);
    printf("✓ Reused count: %lu (should be close to NUM_THREADS * OPS_PER_THREAD)\n", 
           queue.stats.reused_count);
    
    // With thread-local freelists, we should see high reuse
    float reuse_ratio = (float)queue.stats.reused_count / 
                        (queue.stats.reused_count + queue.stats.malloc_count);
    printf("✓ Reuse ratio: %.2f%% (high ratio confirms thread-local freelists working)\n", 
           reuse_ratio * 100);
    
    assert(reuse_ratio > 0.9);  // Should reuse at least 90%
    printf("Test 6 PASSED\n");
}

int main() {
   
    
    test_sequential();
    test_concurrent_basic();
    test_producer_consumer();
    test_freelist_reuse();
    test_stress();
    test_thread_local_freelists();
    

    
    return 0;
}


//test for ex4

// #include "queue_split_lock_local_FL.h"
// #include <iostream>
// #include <thread>
// #include <vector>
// #include <mutex>

// constexpr int N_THREADS = 4;
// constexpr int ITEMS_PER_THREAD = 5;

// QueueSplitLockLocalFL queue;
// std::mutex print_lock; // serialize printing

// void worker(int tid) {
//     for (int i = 1; i <= ITEMS_PER_THREAD; i++) {
//         int val = tid * 100 + i; // encode thread ID and item
//         queue.enq(val);

//         // safe print
//         {
//             std::lock_guard<std::mutex> guard(print_lock);
//             std::cout << "Thread " << tid << " enqueued " << val << "\n";
//         }
//     }
// }

// int main() {
//     queue.queue_init();

//     std::vector<std::thread> threads;
//     for (int t = 0; t < N_THREADS; t++) {
//         threads.emplace_back(worker, t);
//     }

//     for (auto &th : threads) th.join();

//     std::cout << "\nDequeuing all items:\n";

//     int val;
//     while (queue.deq(&val)) {
//         std::cout << "Dequeued value " << val << "\n";
//     }

//     queue.queue_destroy();
//     return 0;
// }
