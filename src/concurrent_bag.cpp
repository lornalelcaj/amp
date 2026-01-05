//concurrent_bag.cpp
#include "concurrent_bag.h"
#include "queue_lock_free_local_FL.h"

ConcurrentBag::ConcurrentBag(std::vector<std::unique_ptr<IQueue>> internal_queues)
    : qs_(std::move(internal_queues)),
      n_(static_cast<int>(qs_.size()))
{
    assert(n_ > 0);
    for (auto& q : qs_) {
        assert(q && "ConcurrentBag: internal queue must not be null");
    }
}

void ConcurrentBag::queue_init() {
    // Initializes IQueue's private stats_lock used for aggregating tls_stats.
    IQueue::queue_init();

    // Initialize internal queues once (single-threaded).
    for (auto& q : qs_) {
        q->queue_init();
    }

    rr_enq_.store(0, std::memory_order_relaxed);
    rr_deq_.store(0, std::memory_order_relaxed);
}

void ConcurrentBag::queue_destroy() {
    // Destroy internal queues (single-threaded).
    for (auto& q : qs_) {
        q->queue_destroy();
    }

    qs_.clear();
    n_ = 0;
}

void ConcurrentBag::thread_prepare() {
    // Reset TLS stats exactly once per OpenMP thread.
    IQueue::thread_prepare();
}

void ConcurrentBag::thread_cleanup() {
    // Aggregate TLS stats exactly once per OpenMP thread.
    IQueue::thread_cleanup();
}

void ConcurrentBag::enq(value_t v) {
    assert(n_ > 0);

    // Count enqueues at bag level (independent of internal queue’s counting).
    tls_stats.enq_count++;

    const uint64_t ticket = rr_enq_.fetch_add(1, std::memory_order_relaxed);
    const int idx = static_cast<int>(ticket % static_cast<uint64_t>(n_));

    qs_[idx]->enq(v);
}

int ConcurrentBag::deq(value_t* v) {
    assert(n_ > 0);
    assert(v != nullptr);

    // Choose a starting point in round-robin order, then scan all queues.
    const uint64_t ticket = rr_deq_.fetch_add(1, std::memory_order_relaxed);
    const int start = static_cast<int>(ticket % static_cast<uint64_t>(n_));

    for (int i = 0; i < n_; ++i) {
        const int idx = (start + i) % n_;
        if (qs_[idx]->deq(v)) {
            tls_stats.deq_count++;
            tls_stats.dequeued_values.push_back(*v);
            return 1;
        }
    }

    tls_stats.failed_deq_count++;
    return 0;
}

IQueue* make_concurrent_bag_lockfree_localfl() {
    const int n = omp_get_max_threads();
    assert(n > 0);

    std::vector<std::unique_ptr<IQueue>> qs;
    qs.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        qs.push_back(std::make_unique<QueueLockFreeLocalFL>());
    }

    return new ConcurrentBag(std::move(qs));
}
