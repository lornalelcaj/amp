import ctypes
import os
import datetime
import itertools as it

class CThreadStats(ctypes.Structure):
    _fields_ = [
        ("enq_count", ctypes.c_uint64),
        ("deq_count", ctypes.c_uint64),
        ("failed_deq_count", ctypes.c_uint64),
        ("duration_ns", ctypes.c_uint64),

        ("freelist_pushes", ctypes.c_uint64),
        ("freelist_pops", ctypes.c_uint64),
        ("freelist_max_size", ctypes.c_uint64),
        ("malloc_count", ctypes.c_uint64),
        ("reused_count", ctypes.c_uint64),

        ("successful_CAS_ops", ctypes.c_uint64),
        ("failed_CAS_ops", ctypes.c_uint64),
    ]

basedir = os.path.dirname(os.path.abspath(__file__))
lib = ctypes.CDLL(f"{basedir}/queue_benchmark.so")

lib.run_queue_benchmark.argtypes = [
    ctypes.c_int,    # threads
    ctypes.c_int,    # max_enq_batches
    ctypes.POINTER(ctypes.c_int),     # enq_batches
    ctypes.POINTER(ctypes.c_int),     # deq_batches
    ctypes.c_int,    # queue_type
    ctypes.c_double, # max_duration_sec
    ctypes.c_bool,     # check_dequeued_values
    ctypes.c_bool,     # print_results
]
lib.run_queue_benchmark.restype = CThreadStats

def write_avg_data(stats, name, time):
    '''
    Writes averages for each point measured into a dataset in the data
    folder timestamped when the run was started.
    '''

    try:
        os.makedirs(f"{basedir}/data/{time}/avg")
    except FileExistsError:
        pass
    with open(f"{basedir}/data/{time}/avg/{name}.data", "w")\
            as datafile:
        datafile.write(f"x datapoint\n")
        for x, box in stats:
            datafile.write(f"{x} {sum(box)/len(box)}\n")

def print_stat_raw(stat: CThreadStats):
    for field in stat._fields_:
        print(field[0], getattr(stat, field[0]))

def print_stat(stat: dict[str, float]):
    for field, value in stat.items():
        print(field, value)

def average_stats(stats: list[CThreadStats]) -> dict[str, float]:
    avg_stats = {}
    for stat in stats:
        for field in stat._fields_:
            avg_stats[field[0]] = avg_stats.get(field[0], 0) + getattr(stat, field[0])

    for field in avg_stats.keys():
        avg_stats[field] /= len(stats)
    return avg_stats

def run():
    stats = []
    concurrent_queue_types = range(1, 6)
    thread_counts = [1, 2, 8, 10, 20, 32, 45, 64]
    batch_sizes = [1, 1000]
    time_limits_s = [1, 5] # time alloted per experiement
    max_number_enq_batches = 10000 # maximum number of enqueue batches per experiment
    configs = ['a', 'b', 'c', 'd']
    repeats = 10
    test_deq_values = False
    print_results = False

    # testing:
    if False:
        configs = ['a']
        repeats = 3
        thread_counts = [31]
        batch_sizes = [1000]
        time_limits_s = [1]
        concurrent_queue_types = [5]
        print_results = True

    #sequential queue
    for batch_size, time_limit in it.product(batch_sizes, time_limits_s):
        print(f'run sequential batchsizes:{batch_size} time:{time_limit}')
        for _ in range(repeats):
            IntArray = ctypes.c_int * 1
            enq_batches = IntArray(*([batch_size] * 1))
            deq_batches = IntArray(*([batch_size] * 1))
            stats.append(lib.run_queue_benchmark(
                1, # number threads
                max_number_enq_batches,
                enq_batches,
                deq_batches,
                0, # queue type 0 is sequential
                time_limit,
                test_deq_values,
                print_results
            ))
    
    
    for queue_type, thread_count_p, batch_size, time_limit in it.product(concurrent_queue_types, thread_counts, batch_sizes, time_limits_s):
        stats = []
        IntArray = ctypes.c_int * thread_count_p
        enq_batches = IntArray()
        deq_batches = IntArray()
        for config in configs:
            stats = []
            print(f'\n* Run config {config}: \n**** concurrent Q:{queue_type} threads:{thread_count_p} batchsizes:{batch_size} time:{time_limit}')
            match config:
                case 'a':
                    # conf a) all threads enqueing and dequeuing with the same batch sizes
                    enq_batches = IntArray(*([batch_size] * thread_count_p))
                    deq_batches = IntArray(*([batch_size] * thread_count_p))
                case 'b':
                    # conf b) one thread enqueing, all other threads dequeuing
                    enq_batches = IntArray(*( [batch_size] + [0] * (thread_count_p - 1) ))
                    deq_batches = IntArray(*( [0] + [batch_size] * (thread_count_p - 1) ))
                case 'c':
                    # conf c)  all threads with id smaller than p/2 enqueing only, the other threads dequeuing only 
                    enq_batches = IntArray(*( [batch_size] * (thread_count_p // 2) + [0]          * (thread_count_p - (thread_count_p // 2)) ))
                    deq_batches = IntArray(*( [0]          * (thread_count_p // 2) + [batch_size] * (thread_count_p - (thread_count_p // 2)) ))
                case 'd':
                    # conf d) even numbered threads enqueing, odd numbered threads dequeuing
                    enq_batches = IntArray(*( batch_size if j % 2 == 0 else 0 for j in range(thread_count_p) ))
                    deq_batches = IntArray(*( 0 if j % 2 == 0 else batch_size for j in range(thread_count_p) ))

            for _ in range(repeats):
                stats.append(lib.run_queue_benchmark(
                    thread_count_p,
                    max_number_enq_batches,
                    enq_batches,
                    deq_batches,
                    queue_type,
                    time_limit,
                    test_deq_values,
                    print_results
                ))

            print(f'\nResults config:{config} concurrent Q:{queue_type} threads:{thread_count_p} batchsizes:{batch_size} time:{time_limit}')
            print_stat(average_stats(stats))

    #write_avg_data(stats, "test", now)

if __name__ == "__main__":
    run()

