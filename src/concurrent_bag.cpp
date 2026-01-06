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
            return 1;
        }
    }

    return 0;
}
