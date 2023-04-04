import copy
import gzip
import json
import sys
import threading
import time
from multiprocessing.managers import SharedMemoryManager
from pathlib import Path

from algorithms.GenericAlgorithm import *
from algorithms.ARC import ARC
from algorithms.CAR import CAR
from algorithms.LRU_K import LRU_K
from algorithms.CLOCK import CLOCK
import argparse
from multiprocessing import Process, Value, Condition, shared_memory, Barrier
import os
import shutil

import numpy as np


def nn(*arguments):
    for arg in arguments:
        assert arg is not None


def rm_r(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    elif os.path.exists(path):
        os.remove(path)


def overall_manhattan_distance(sorted_tl1, sorted_baseline_tl, punish=False):
    sum_ = 0

    def bsearch(page, slist):
        low = 0
        high = len(slist) - 1
        mid = 0
        while low <= high:
            mid = (high + low) // 2
            if slist[mid][1] < page:
                low = mid + 1
            elif slist[mid][1] > page:
                high = mid - 1
            else:
                return mid
        return -1

    for hotness, page in sorted_baseline_tl:
        idx_in_tl1 = bsearch(page, sorted_tl1)
        if idx_in_tl1 != -1:
            sum_ += abs(hotness - sorted_tl1[idx_in_tl1][0])
        elif punish:
            sum_ += hotness
    return sum_


def reader_process(file_to_read, shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items,
                   total_nm_processes, shared_ready_cv, num_ready_shared_value, continue_shared_value):
    nn(file_to_read, shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items,
       total_nm_processes, shared_ready_cv, num_ready_shared_value, continue_shared_value)
    # At the beginning, so the processes wait for this reader process to be ready
    assert num_ready_shared_value.value != 0
    assert continue_shared_value.value
    id_str = "READER PROCESS -"
    n = 0
    with open(file_to_read, 'r') as f:
        shared_mem_accesses_list = shared_memory.SharedMemory(create=False, name=shared_mem_accesses_list_name)
        shared_mem_access_type_list = shared_memory.SharedMemory(create=False, name=shared_mem_access_type_list_name)
        mem_accesses_arr = np.ndarray(n_items, dtype=np.uintp, buffer=shared_mem_accesses_list.buf)
        mem_accesses_type_arr = np.ndarray(n_items, dtype=np.uint8, buffer=shared_mem_access_type_list.buf)

        def fill_array():
            for i in range(n_items):
                line = f.readline()
                if line == '':
                    return 0
                is_load = line[0] == 'R'
                addr = int(line[1:], 16)
                mem_accesses_arr[i] = addr
                mem_accesses_type_arr[i] = is_load
            return n_items

        def stop_condition(read):
            return read > 800_000_000

        # First fill
        total_read = n_items
        setup_success = False
        if fill_array():
            total_read += n_items
            setup_success = True
        while setup_success and not stop_condition(total_read):
            print(f"{id_str} n={n}")
            # Wake Alg processes
            with shared_ready_cv:
                num_ready_shared_value.value = 0
                shared_ready_cv.notify_all()
                shared_ready_cv.wait_for(lambda: num_ready_shared_value.value == total_nm_processes)
            # print(f"{id_str} ALGs finished")

            # Get new data
            if fill_array():
                total_read += n_items
            else:
                break
            n += 1
        print("Reader process instructed to end, switching continue var")
        continue_shared_value.value = False
        # Wait for all processes to have finished and notify them it's over
        with shared_ready_cv:
            shared_ready_cv.wait_for(lambda: num_ready_shared_value.value == total_nm_processes)
            num_ready_shared_value.value = 0
            shared_ready_cv.notify_all()


MAX_BUFFER_SIZE_BYTES = 1 * 1024 * 1024
PTCHANGE_DIR_NAME = 'ptchange'
OTHER_DATA_FN = "stats.csv"


class AlgWArgs:
    def __init__(self, alg, dir, sample_rate, ratio):
        self.alg = copy.deepcopy(alg)
        self.dir = dir  # != None === must do standalone as well
        self.sample_rate = sample_rate
        self.ratio = ratio


def comparison_and_standalone(shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items,
                              shared_process_ready_cv, num_alg_ready_shared_value,
                              continue_shared_value, alg_processes_barrier,
                              alg_tuple: tuple, comp_write_dir: str):
    nn(shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items, shared_process_ready_cv,
       num_alg_ready_shared_value, continue_shared_value, alg_tuple, comp_write_dir)
    assert continue_shared_value.value
    id_str = f"{os.getpid()}:{threading.current_thread().ident} -"
    is_load = False
    for alg in alg_tuple:
        assert 0 < alg.sample_rate <= 1
        if alg.dir is not None:
            alg.ptchange_dir = alg.dir + PTCHANGE_DIR_NAME
            Path(alg.ptchange_dir).mkdir(parents=False, exist_ok=False)
            alg.ptchange_dir += '/'
            alg.ptchange = []
            alg.n_pfaults = alg.curr_pfault_distance_sum = alg.curr_non_pfault_dist_sum = 0
            alg.cur_sorted_tl = []
        alg.prev_page_base, alg.repeat = -1, False
        alg.considered_loads, alg.considered_stores = 0, 0

        def should_consider():
            return ((alg.considered_loads + alg.considered_stores) / seen) <= alg.sample_rate

        def should_consider_ratio():
            return should_consider() and ((is_load and ((alg.considered_loads / alg.considered_stores) <= alg.ratio))
                                          or (not is_load and (
                            alg.ratio <= (alg.considered_loads / alg.considered_stores))))

        alg.consideration_method = should_consider_ratio if alg.ratio >= 0 else should_consider

    shared_mem_accesses_list = shared_memory.SharedMemory(create=False, name=shared_mem_accesses_list_name)
    shared_mem_access_type_list = shared_memory.SharedMemory(create=False, name=shared_mem_access_type_list_name)
    mem_accesses_arr = np.ndarray(n_items, dtype=np.uintp, buffer=shared_mem_accesses_list.buf)
    mem_accesses_type_arr = np.ndarray(n_items, dtype=np.uint8, buffer=shared_mem_access_type_list.buf)

    print(f"{id_str} Waiting for first fill and starting...")
    n_writes = 0
    seen = 0  # count the total considered and seen memory instructions
    while True:
        # Wait to be notified you can go ; === value to be 0
        with shared_process_ready_cv:
            shared_process_ready_cv.wait_for(lambda: num_alg_ready_shared_value.value == 0)

        # Wait for all processes to be able to start to run, o/w/ DDLCK if one finishes before all have left the wait
        alg_processes_barrier.wait()

        if not continue_shared_value.value:
            break

        comp_diffs = []
        for i in range(n_items):
            is_load = bool(mem_accesses_type_arr[i])
            mem_address = int(mem_accesses_arr[i])
            page_base = page_start_from_mem_address(mem_address)
            for alg in alg_tuple:
                alg.changed = False
                seen += 1
                if seen % 100_000_000 == 0:
                    print(
                        f"{id_str} - Reached seen = {seen}\nSR={alg.requested_sample_rate},#T={alg.considered_loads + alg.considered_stores} (#S={alg.considered_stores},#L={alg.considered_loads}{f', gt_ratio={alg.ratio}, curr_ratio={alg.considered_loads / alg.considered_stores}' if alg.ratio != -1 else ''}), n_writes={n_writes},i={i}")
                if alg.prev_page_base == page_base:
                    alg.repeat = True
                else:
                    alg.prev_page_base = page_base
                    alg.repeat = False
                pfault = alg.alg.is_page_fault(page_base)
                if pfault or alg.consideration_method():
                    if is_load:
                        alg.considered_loads += 1
                    else:
                        alg.considered_stores += 1
                    if alg.repeat:
                        # No need to consume : all algorithms have already correctly handled the page
                        # Also no need to update running averages, as md will be 0 if prev_tl = cur_tl
                        continue
                    alg.changed = True
                    alg.alg.consume(page_base)
                    new_tl = sorted(list(enumerate(alg.alg.get_temperature_list())), key=lambda tpl: tpl[1])
                    if alg.dir is not None:
                        # Must capture standalone data as well
                        md = overall_manhattan_distance(new_tl, alg.cur_sorted_tl, False)
                        if pfault:
                            alg.n_pfaults += 1
                            alg.curr_pfault_distance_sum += md
                        else:
                            alg.curr_non_pfault_dist_sum += md
                        alg.ptchange.append((seen, md))
                    alg.cur_sorted_tl = new_tl
            # Now gather comparison info
            # Only update comparison when one of the tuples has changed:
            if alg_tuple[0].changed or alg_tuple[1].changed:
                comp_diffs.append(
                    (seen, overall_manhattan_distance(alg_tuple[0].cur_sorted_tl, alg_tuple[1].cur_sorted_tl, True)))

        for alg in alg_tuple:
            if alg.dir is not None:
                # Save data to file:
                save_to_file_compressed(np.array(alg.ptchange).transpose(), id_str, alg.ptchange_dir, n_writes)
                with open(alg.dir + OTHER_DATA_FN, 'w') as f:
                    f.write(
                        f"seen,considered_l,considered_s,pfaults,pfault_dist_average,non_pfault_dist_average\n{seen},{alg.considered_loads},{alg.considered_stores},{alg.n_pfaults},{alg.curr_pfault_distance_sum / alg.n_pfaults},{alg.curr_non_pfault_dist_sum / (alg.considered_loads + alg.considered_stores - alg.n_pfaults)}\n")
                alg.ptchange = []
        # Save comparisons
        save_to_file_compressed(np.array(comp_diffs).transpose(), id_str, comp_write_dir, n_writes)
        n_writes += 1



        # Say we're ready!
        with shared_process_ready_cv:
            num_alg_ready_shared_value.value += 1
            shared_process_ready_cv.notify_all()

    shared_mem_accesses_list.close()
    shared_mem_access_type_list.close()
    print(f"{id_str} Finished file reading")


def save_to_file_compressed(np_array, id_str, savedir, number_writes):
    # print(id_str, f"Saving, n_writes={number_writes}")
    f = gzip.GzipFile(savedir + str(number_writes) + '.npy.gz', 'w')
    np.save(file=f, arr=np_array)
    f.close()


REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO = 0.01
AVERAGE_SAMPLE_RATIO = 0.05
N_ITEMS = 1024 * 512


def main(args):
    K = 2
    MAX_PAGE_CACHE_SIZE = 507569  # pages = `$ ulimit -l`/4  ~= 2GB mem
    page_cache_size = MAX_PAGE_CACHE_SIZE // 16  # ~ 64 KB mem
    samples_div = [i / 100 for i in range(0, 99, 33) if i != 0]
    samples_div = [REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO, AVERAGE_SAMPLE_RATIO] + samples_div + [1.0]
    algs = [LRU_K(page_cache_size, K, 0, False), CLOCK(page_cache_size, K), ARC(page_cache_size), CAR(page_cache_size)]
    alg_names = [alg.name() for alg in algs]
    # We want:
    #   standalone alg w/ diff levels of mem traces --> (page faults + extra) ~=  TESTED_VALUE * full_memory_trace
    #                               where TESTED_VALUE is in {20%, 40%, 60%, 80%} ; vs 100% (full trace)
    #   intra-group alg w/ same level of mem trace; e.g.: LRU_40% vs CLOCK_40%
    #   inter-group alg w/ same level of mem traces; e.g.: CLOCK_60% vs CAR_60%
    #   if args.ratio is set, a tested value of 1% is also done for standalone, where ratio of mem accesses is preserved
    # Each standalone alg will be compared with, 1) standalone with full memory trace, 2) intra w/ same mem trace,
    # 3) inter with same mem_trace, except for standalone with full memtrace, as it doesn't make sense comparing it
    # with himself --> for all divs, we have only 3 queues, for FULL_MEM_TRACE, we have 2+len(samples_div)-1 Qs.
    # For ratio, we'll simply want to compare as ratio-preserved as done with any non-full div before, as well as
    # ratio to non_ratio for REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO size --> 3+3 more queues
    non_ratio_comparisons = []
    ratio_comparisons = []
    # 1) Standalone and ratio
    for alg in algs:
        for div in samples_div:
            if div != 1:
                compared_alg_name, baseline_alg_name = f"{alg.name()}_{str(div)}", f"{alg.name()}_1.0"
                non_ratio_comparisons.append(f"{compared_alg_name} vs {baseline_alg_name}")
        # 1).ratio
        if args.ratio_realistic:
            compared_alg_name, baseline_alg_name = f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}_R", f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}"
            ratio_comparisons.append(f"{compared_alg_name} vs {baseline_alg_name}")

    # 2)
    for alg1, alg2 in zip(algs[::2], algs[1::2]):
        for div in samples_div:
            compared_alg_name, baseline_alg_name = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg_name} vs {baseline_alg_name}")

    # 3)
    for alg1, alg2 in zip(algs[:2], algs[2:]):
        for div in samples_div:
            compared_alg_name, baseline_alg_name = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg_name} vs {baseline_alg_name}")

    base_dir = args.data_save_dir.resolve().as_posix() + '/'
    standalone_dir = Path(base_dir + "standalone")
    standalone_dir.mkdir(exist_ok=False, parents=False)
    standalone_dir_as_posix = standalone_dir.resolve().as_posix() + '/'
    comp_dir = Path(base_dir + "comp")
    comp_dir.mkdir(exist_ok=False, parents=False)
    comp_dir_as_posix = comp_dir.resolve().as_posix() + '/'

    num_comp_processes = len(ratio_comparisons) + len(non_ratio_comparisons)

    with SharedMemoryManager() as smm:
        base_arrays = [None, None]
        base_arrays[0] = np.zeros(N_ITEMS, dtype=np.uintp)
        base_arrays[1] = np.zeros(N_ITEMS, dtype=np.uint8)
        shared_mem = [None, None]
        copied_arrays = [None, None]
        for i in range(len(base_arrays)):
            shared_mem[i] = smm.SharedMemory(size=base_arrays[i].nbytes)
            copied_arrays[i] = np.ndarray(base_arrays[i].shape, dtype=base_arrays[i].dtype, buffer=shared_mem[i].buf)
            copied_arrays[i][:] = base_arrays[i][:]
        cv = Condition()
        shared_value = Value('B', lock=False)
        shared_value.value = num_comp_processes
        shared_barrier = Barrier(num_comp_processes)
        continue_shared_value = Value('B', lock=False)
        continue_shared_value.value = 1

        standalone_algs = set()
        all_processes = []
        sys.setswitchinterval(1.0)
        for comparison in ratio_comparisons + non_ratio_comparisons:
            tpl = tuple(comparison.split(" vs "))
            out_alg_args = []
            already_chose = False
            for idx, name in enumerate(tpl):
                a_split = name.split('_')
                a_ratio = 'R' in a_split
                if a_ratio:
                    a_split.remove('R')
                a_non_div_name = '_'.join(a_split[:-1])
                awa = AlgWArgs(algs[alg_names.index(a_non_div_name)],
                               None,
                               float(a_split[-1]),
                               args.db[args.mem_trace_path.resolve().as_posix()]['ratio'] if a_ratio else -1)
                if name not in standalone_algs and not already_chose:
                    alg_dir = Path(standalone_dir_as_posix + name)
                    alg_dir.mkdir(exist_ok=False, parents=False)
                    awa.dir = alg_dir.resolve().as_posix() + '/'
                    standalone_algs.add(name)
                out_alg_args.append(awa)
            comp_save_dir = Path(comp_dir_as_posix + tpl[0] + '_vs_' + tpl[1])
            comp_save_dir.mkdir(exist_ok=False, parents=False)
            p = Process(target=comparison_and_standalone, args=(shared_mem[0].name, shared_mem[1].name, N_ITEMS,
                                                                cv, shared_value, continue_shared_value, shared_barrier,
                                                                tuple(out_alg_args),
                                                                comp_save_dir.resolve().as_posix() + '/'))
            p.start()
            all_processes.append(p)

        # Start all algorithms
        print("Starting reader process")
        r_process = Process(target=reader_process, args=(args.mem_trace_path.resolve().as_posix(),
                                                         shared_mem[0].name, shared_mem[1].name, N_ITEMS,
                                                         num_comp_processes, cv, shared_value, continue_shared_value))
        r_process.start()
        r_process.join()

        for p in all_processes:
            p.join()

    print("Got all data!")


def parse_args():
    parser = argparse.ArgumentParser(
        prog="ptrace_data",
        description="Script to simulate different algorithms' behavior given different completeness of memory traces ",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-r', '--ratio-realistic', action=argparse.BooleanOptionalAction, default=True,
                        help="Flag to specify if one "
                             "wants to also include a workload-resemblant, realistic memory trace ")
    parser.add_argument('--db-file', type=str, default='db.json',
                        help="File (+path) containing (cached) kvs containting "
                             "mem_trace_path->stats mappings. If file not "
                             "existant, automaticatlly gets created.")
    parser.add_argument('--always-overwrite', default=False, action=argparse.BooleanOptionalAction,
                        help="Overwrite flag to always allow overwrite of save files")
    parser.add_argument("--data-save-dir", default="results/%mtp/%tst",
                        help="Directory where data is to be saved. Can use %%mtp to represent the benchmark name "
                             "of the memory traces; %%tst to represent a timestamp; %%%% to represent a"
                             "percentage sign.")

    parser.add_argument('mem_trace_path', type=str, help="Path to file containing the ground truth (PIN) memory trace")
    arg_ns = parser.parse_args()
    arg_ns.mem_trace_path = Path(arg_ns.mem_trace_path)
    if not arg_ns.mem_trace_path.exists() or not arg_ns.mem_trace_path.is_file():
        parser.error("Invalid mem_trace path: file does not exist!")
        exit(-1)

    KNOWN_BENCHMARKS = ["pmbench", "stream"]
    bm_name = "unknown"
    for bm in KNOWN_BENCHMARKS:
        if bm in arg_ns.mem_trace_path.resolve().as_posix():
            bm_name = bm
            break
    if "!" in arg_ns.data_save_dir:
        parser.error("Invalid data save dir, '!' detected")
        exit(-1)
    timestr = time.strftime("%Y%m%d-%H%M%S")
    arg_ns.data_save_dir = Path(arg_ns.data_save_dir.replace("%%", "!")
                                .replace("%mtp", bm_name)
                                .replace("%tst", timestr)
                                .replace("!", "%"))
    if arg_ns.data_save_dir.exists() and not arg_ns.data_save_dir.is_dir():
        print("Data save dir is not a directory. Exiting...")
        exit(-1)
    arg_ns.data_save_dir.mkdir(parents=True, exist_ok=True)
    arg_ns.db_file = Path(arg_ns.db_file).resolve()
    arg_ns.db_file.parent.mkdir(parents=True, exist_ok=True)
    return arg_ns


def populate_or_get_db(args):
    nn(args, args.db_file, args.mem_trace_path)
    in_file = None
    if args.db_file.exists():
        mode = 'r+'
    else:
        in_file = False
        mode = 'w'
    with open(args.db_file.resolve().as_posix(), mode) as dbf:
        if in_file is None:  # === db_file.exists()
            try:
                db = json.load(dbf)
            except IOError as e:
                print(e, "Couldn't parse JSON data from DB, exiting...")
                exit(-1)
        else:
            db = {}
        full_path = args.mem_trace_path.resolve().as_posix()
        in_file = full_path in db.keys()
        if not in_file:
            with open(full_path, 'r') as mtf:
                # Get the data: We want #lds,#stores; to deduce ratio and total count
                lds = strs = 0
                while (line := mtf.readline()) != '':
                    if line[0] == 'R':
                        lds += 1
                    else:
                        assert line[0] == 'W'
                        strs += 1
            db[full_path] = {"loads": lds, 'stores': strs, 'ratio': round(lds / strs, 4), 'count': lds + strs}
            # And save it
            dbf.seek(0)
            json.dump(db, dbf)
            exit(1)
    return db


if __name__ == '__main__':
    args = parse_args()
    args.db = populate_or_get_db(args)
    main(args)
