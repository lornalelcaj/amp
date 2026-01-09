import ctypes
import os
import datetime
import itertools as it
import sys

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
time_stamp = datetime.datetime.now()
lib = ctypes.CDLL(f"{basedir}/queue_benchmark.so")

lib.run_queue_benchmark.argtypes = [
    ctypes.c_int,       # threads
    ctypes.c_ulong,     # max_enq_values
    ctypes.POINTER(ctypes.c_int),     # enq_batches
    ctypes.POINTER(ctypes.c_int),     # deq_batches
    ctypes.c_int,    # queue_type
    ctypes.c_double, # max_duration_sec
    ctypes.c_bool,   # check_dequeued_values
    ctypes.c_bool,   # print_results
    ctypes.c_bool,   # print_info
]
lib.run_queue_benchmark.restype = CThreadStats

def write_data(stats: list[dict[str, float]], name, time):
    '''
    Writes averages for each point measured into a dataset in the data
    folder timestamped when the run was started.
    '''
    try:
        os.makedirs(f"{basedir}/data/{time}")
    except FileExistsError:
        pass
    with open(f"{basedir}/data/{time}/{name}.data", "a") as datafile:
        # write header
        col_order = list(stats[0].keys())
        datafile.write(f"{' '.join(col_order)}\n")
        # write data
        for stat in stats:
            datafile.write(f"{' '.join(map(lambda c: str(stat[c]), col_order))}\n")
        datafile.flush()

def make_data_file(cols: list[str], file_name, benchmark_name):
    folder_name = f'{benchmark_name}_{time_stamp.strftime("%Y-%m-%d-T%Hh%Mm%Ss")}'
    try:
        os.makedirs(f"{basedir}/data/{folder_name}")
    except FileExistsError:
        pass
    file = open(f"{basedir}/data/{folder_name}/{file_name}.data", "w")
    file.write(f"{' '.join(cols)}\n")
    return file

def write_partial_data(stats: list[dict[str, float]], datafile, col_order):
    for stat in stats:
        datafile.write(f"{' '.join(map(lambda c: str(stat[c]), col_order))}\n")
    datafile.flush()

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

def summarize_results(stats: list[CThreadStats]) -> dict[str, float]:
    summary = average_stats(stats)
    summary['throughput'] = (summary['enq_count'] + summary['deq_count']) / summary['duration_ns'] * 1e3
    return summary


def get_batch_arrays(config, batch_size, thread_count_p):
    IntArray = ctypes.c_int * thread_count_p
    enq_batches = IntArray()
    deq_batches = IntArray()
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
    return enq_batches, deq_batches
    
def run_concurrent_queue_experiments(
        benchmark_name,
        queue_type, thread_counts, configs,
        batch_sizes, time_limits_s, max_number_enq_values, repeats, total_values,
        data_columns, test_deq_values = False, print_results = False, print_info = False):
    
    with make_data_file(data_columns, f'concurrent_queue_Q{queue_type}', benchmark_name) as file:
        print(f'\n*** Queue {queue_type} Experiments ***')
        for batch_size, time_limit in it.product(batch_sizes, time_limits_s):
            data_points = []
            print(f'* Q:{queue_type} batchsizes:{batch_size} time:{time_limit} *', flush=True)
            for config, thread_count_p in it.product(configs, thread_counts):          
                print(f'Run config:{config} threads:{thread_count_p}')
                enq_batches, deq_batches = get_batch_arrays(config, batch_size, thread_count_p)

                stats = []
                for _ in range(repeats):
                    stats.append(lib.run_queue_benchmark(
                        thread_count_p,
                        max_number_enq_values,
                        enq_batches,
                        deq_batches,
                        queue_type,
                        time_limit,
                        test_deq_values,
                        print_results,
                        print_info
                    ))

                data_point = {
                    'queue_type': queue_type,
                    'n_threads': thread_count_p,
                    'config': config,
                    'batch_size': batch_size,
                    'time_limit': time_limit,
                    'total_values': total_values
                }
                data_point.update(summarize_results(stats))
                data_points.append(data_point)
            write_partial_data(data_points, file, data_columns)

def run_sequential_queue_experiments(
        benchmark_name,
        batch_sizes, time_limits_s, max_number_enq_values, repeats, total_values,
        data_columns, test_deq_values = False, print_results = False, print_info = False):
    data_points = []
    for batch_size, time_limit in it.product(batch_sizes, time_limits_s):
        print(f'run sequential batchsizes:{batch_size} time:{time_limit}')
        stats = []
        for _ in range(repeats):
            IntArray = ctypes.c_int * 1
            enq_batches = IntArray(*([batch_size] * 1))
            deq_batches = IntArray(*([batch_size] * 1))
            stats.append(lib.run_queue_benchmark(
                1, # number threads
                max_number_enq_values,
                enq_batches,
                deq_batches,
                0, # queue type 0 is sequential
                time_limit,
                test_deq_values,
                print_results,
                print_info
            ))

        data_point = {
            'queue_type': 0,
            'n_threads': 1,
            'config': 'a',
            'batch_size': batch_size,
            'time_limit': time_limit,
            'total_values': total_values
        }
        data_point.update(summarize_results(stats))
        data_points.append(data_point)
    with make_data_file(data_columns, f'sequential_queue', benchmark_name) as file:
            write_partial_data(data_points, file, data_columns)

def run_benchmark(
        benchmark_name = '',
        concurrent_queue_types = [1, 2, 3, 4, 5, 6],
        thread_counts = [1, 8, 20, 45],
        batch_sizes = [1, 100],
        time_limits_s = [0.25, 1], # time alloted per experiement
        max_number_enq_values = 10000, # maximum number of enqueue values per experiment
        configs = ['a', 'b', 'c', 'd'],
        repeats = 10,
        test_deq_values = False,
        print_results = False,
        print_info = False):

    data_columns = [
        'queue_type',
        'n_threads',
        'config',
        'batch_size',
        'time_limit',
        'total_values',
        'throughput'
    ] + list(map(lambda f: f[0], CThreadStats()._fields_))
    print(f'*-*-*-*-* Run Benchmark {benchmark_name} *-*-*-*-*')

    run_sequential_queue_experiments(
        benchmark_name,
        batch_sizes, 
        time_limits_s, 
        max_number_enq_values, 
        repeats, 
        max_number_enq_values,
        data_columns, 
        test_deq_values, 
        print_results, 
        print_info)
    
    for queue_type in concurrent_queue_types:
        run_concurrent_queue_experiments(
            benchmark_name,
            queue_type,
            thread_counts,
            configs,
            batch_sizes, 
            time_limits_s, 
            max_number_enq_values, 
            repeats, 
            max_number_enq_values,
            data_columns, 
            test_deq_values, 
            print_results, 
            print_info
        )
    print(f'\nDone - Total Time Taken: {datetime.datetime.now() - time_stamp}')

if __name__ == "__main__":
    if (len(sys.argv) == 1):
        # full benchmark
        run_benchmark(
            benchmark_name= 'benchmark',
            concurrent_queue_types = [1, 2, 3, 4, 5, 6],
            thread_counts = [1, 2, 8, 10, 20, 32, 45, 64],
            batch_sizes = [1, 1000],
            time_limits_s = [1, 5], # time alloted per experiement
            max_number_enq_values = 100000, # maximum number of enqueue values per experiment
            configs = ['a', 'b', 'c', 'd'],
            repeats = 10
        )
    if ('-c' in sys.argv[1:]):
        # consistency check
        run_benchmark(
            benchmark_name= 'consistency',
            concurrent_queue_types = [1, 2, 3, 4, 5, 6],
            thread_counts = [1, 33],
            batch_sizes = [1, 10],
            time_limits_s = [1], # time alloted per experiement
            max_number_enq_values = 1000, # maximum number of enqueue values per experiment
            configs = ['a', 'b', 'c', 'd'],
            repeats = 1,
            test_deq_values= True)
        
    if ('-s' in sys.argv[1:]):
        # small benchmark
        run_benchmark(
            benchmark_name= 'small-bench',
            concurrent_queue_types = [1, 2, 3, 4, 5, 6],
            thread_counts = [1, 8, 20, 64],
            batch_sizes = [1, 1000],
            time_limits_s = [0.25, 1], # time alloted per experiement
            max_number_enq_values = 20000, # maximum number of enqueue values per experiment
            configs = ['a', 'b', 'c', 'd'],
            repeats = 2)
    if ('-t' in sys.argv[1:]):
        # testing
        run_benchmark(
            benchmark_name= 'testing',
            concurrent_queue_types = [6],
            thread_counts = [32],
            batch_sizes = [10],
            time_limits_s = [5],
            max_number_enq_values = 100, # maximum number of enqueue values per experiment
            configs = ['d'], # , 'b', 'c', 'd'
            repeats = 10,
            test_deq_values = True,
            print_results = True,
            print_info = False,
        )

