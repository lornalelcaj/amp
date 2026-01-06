import ctypes
import os
import datetime

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
    ctypes.c_int,    # repetitions
    ctypes.POINTER(ctypes.c_int),     # enq_batches
    ctypes.POINTER(ctypes.c_int),     # deq_batches
    ctypes.c_int,    # queue_type
    ctypes.c_double  # max_duration_sec
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

def run():
    stats = []
    threads = [1, 2, 8, 10, 20, 32, 45, 64]
    values = [1, 1000]
    time = [1, 5]
    
    #sequential queue
    for r in range (0,10): # repeat 10 times
        for v in values: # batchsizes
            for t in time: # time alloted per experiement
                IntArray = ctypes.c_int * 1
                enq_batches = IntArray(*([v] * 1))
                deq_batches = IntArray(*([v] * 1))
                stats.append(lib.run_queue_benchmark(
                    1,      
                    10,
                    enq_batches,
                    deq_batches,
                    0,
                    t
                ))
    
    for r in range (0,10): # repeat 10 times
        for i in range (1,6): # Concurrent Queue types
            for p in threads: # number of threads
                for v in values: # batchsizes
                    for t in time: # time alloted per experiment
                        IntArray = ctypes.c_int * p
                        # conf a
                        enq_batches = IntArray(*([v] * p))
                        deq_batches = IntArray(*([v] * p))
                        stats.append(lib.run_queue_benchmark(
                            p,      # threads
                            10,
                            enq_batches,
                            deq_batches,
                            i,
                            t
                        ))
                        # conf b
                        enq_batches = IntArray(*( [v] + [0] * (p - 1) ))
                        deq_batches = IntArray(*( [0] + [v] * (p - 1) ))
                        stats.append(lib.run_queue_benchmark(
                            p,      # threads
                            10,
                            enq_batches,
                            deq_batches,
                            i,
                            t
                        ))
                        # conf c
                        enq_batches = IntArray(*( [v] * (p // 2) + [0] * (p - (p // 2)) ))
                        deq_batches = IntArray(*( [0] * (p // 2) + [v] * (p - (p // 2)) ))
                        stats.append(lib.run_queue_benchmark(
                            p,      # threads
                            10,
                            enq_batches,
                            deq_batches,
                            i,
                            t
                        ))
                        # conf d
                        enq_batches = IntArray(*( v if j % 2 == 0 else 0 for j in range(p) ))
                        deq_batches = IntArray(*( 0 if j % 2 == 0 else v for j in range(p) ))
                        stats.append(lib.run_queue_benchmark(
                            p,      # threads
                            10,
                            enq_batches,
                            deq_batches,
                            i,
                            t
                        ))


    #print("Enq:", stats[0].enq_count)
    #print("Deq:", stats[0].deq_count)

#    for field in stats[0]._fields_:
#        print (field[0], getattr(stats[0], field[0]))

    #write_avg_data(stats, "test", now)

if __name__ == "__main__":
    run()

