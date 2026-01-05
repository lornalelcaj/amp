//concurrent_bag_factory.h
#pragma once

#include <vector>
#include <memory>
#include <cassert>
#include <omp.h>

#include "concurrent_bag.h"
#include "queue_lock_free_local_FL.h"

inline IQueue* make_concurrent_bag_lockfree_localfl() {
    const int n = omp_get_max_threads();
    assert(n > 0);

    std::vector<std::unique_ptr<IQueue>> qs;
    qs.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        qs.push_back(std::make_unique<QueueLockFreeLocalFL>());
    }

    return new ConcurrentBag(std::move(qs));
}
