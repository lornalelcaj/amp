//concurrent_bag.h

#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cassert>
#include <omp.h>

#include "IQueue.h"
#include "thread_stats_tls.h"

// Exercise 7: Concurrent bag (relaxed queue) built from N internal queues.
//
// enq(v): enqueue into internal queues in global round-robin order.
// deq(v): choose a starting queue in global round-robin order and scan all queues;
//         succeed on first dequeue, else return failure.
class ConcurrentBag final : public IQueue {
public:
    explicit ConcurrentBag(std::vector<std::unique_ptr<IQueue>> internal_queues);
    ~ConcurrentBag() override = default;

    void queue_init() override;
    void queue_destroy() override;

    void thread_prepare() override;
    void thread_cleanup() override;

    void enq(value_t v) override;
    int  deq(value_t* v) override;

private:
    std::vector<std::unique_ptr<IQueue>> qs_;
    int n_ = 0;

    std::atomic<uint64_t> rr_enq_{0};
    std::atomic<uint64_t> rr_deq_{0};
};

// Factory for a bag made of QueueLockFreeLocalFL 
IQueue* make_concurrent_bag_lockfree_localfl();
