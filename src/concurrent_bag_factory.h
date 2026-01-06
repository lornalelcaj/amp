//concurrent_bag_factory.h
#pragma once

#include "IQueue.h"

//explicit number of internal queues.
IQueue* make_concurrent_bag_lockfree_localfl(int n_queues);

// Convenience for OpenMP tests (uses omp_get_max_threads()).
IQueue* make_concurrent_bag_lockfree_localfl_omp();
