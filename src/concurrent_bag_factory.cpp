#include "concurrent_bag_factory.h"

#include <vector>
#include <memory>
#include <cassert>
#include <omp.h>

#include "concurrent_bag.h"
#include "queue_lock_free_local_FL.h"

IQueue* make_concurrent_bag_lockfree_localfl(int n_queues) {
    assert(n_queues > 0);

    std::vector<std::unique_ptr<IQueue>> qs;
    qs.reserve(static_cast<size_t>(n_queues));
    for (int i = 0; i < n_queues; ++i) {
        qs.push_back(std::make_unique<QueueLockFreeLocalFL>());
    }
    return new ConcurrentBag(std::move(qs));
}

IQueue* make_concurrent_bag_lockfree_localfl_omp() {
    return make_concurrent_bag_lockfree_localfl(omp_get_max_threads());
}
