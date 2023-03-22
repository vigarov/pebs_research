import copy
import gzip
import json
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
from multiprocessing import Process, Queue, Value, Condition, shared_memory, Barrier
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


def overall_manhattan_distance(tl1, baseline_tl, punish=False):
    sum_ = 0
    page_sorted_tl1 = sorted(list(enumerate(tl1)), key=lambda tpl: tpl[1])
    page_sorted_tl2 = sorted(list(enumerate(baseline_tl)), key=lambda tpl: tpl[1])

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

    for hotness, page in page_sorted_tl2:
        idx_in_tl1 = bsearch(page, page_sorted_tl1)
        if idx_in_tl1 != -1:
            sum_ += abs(hotness - page_sorted_tl1[idx_in_tl1][0])
        elif punish:
            sum_ += hotness
    return sum_


def reader_process(file_to_read, shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items,
                   total_alg_processes, shared_alg_ready_cv, num_alg_ready_shared_value,
                   total_comp_processes, shared_comp_ready_cv, num_comp_ready_shared_value,
                   continue_shared_value):
    nn(file_to_read, shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items, total_alg_processes,
       shared_alg_ready_cv, num_alg_ready_shared_value, total_comp_processes, shared_comp_ready_cv,
       num_comp_ready_shared_value, continue_shared_value)
    # At the beginning, so the processes wait for this reader process to be ready
    assert num_alg_ready_shared_value.value != 0
    assert continue_shared_value.value
    id_str = "READER PROCESS -"
    with open(file_to_read, 'r') as f:
        shared_mem_accesses_list = shared_memory.SharedMemory(create=False, name=shared_mem_accesses_list_name)
        shared_mem_access_type_list = shared_memory.SharedMemory(create=False, name=shared_mem_access_type_list_name)
        mem_accesses_arr = np.ndarray(n_items, dtype=np.uintp, buffer=shared_mem_accesses_list.buf)
        mem_accesses_type_arr = np.ndarray(n_items, dtype=np.uint8, buffer=shared_mem_access_type_list.buf)

        def fill_array():
            for i in range(n_items):
                line = f.readline()
                if line == '':
                    return -1
                is_load = line[0] == 'R'
                addr = int(line[1:], 16)
                mem_accesses_arr[i] = addr
                mem_accesses_type_arr[i] = is_load
            return n_items

        def stop_condition(read):
            return read

        # First fill
        total_read = n_items
        setup_success = False
        if fill_array():
            total_read += n_items
            setup_success = True
        while setup_success and not stop_condition(total_read):
            # Wake Alg processes
            with shared_alg_ready_cv:
                num_alg_ready_shared_value.value = 0
                shared_alg_ready_cv.notify_all()
                # Wait for them to finish
                shared_alg_ready_cv.wait_for(lambda: num_alg_ready_shared_value.value == total_alg_processes)
            print(f"{id_str} ALGs finished")

            # Wake Comparison processes
            with shared_comp_ready_cv:
                num_comp_ready_shared_value.value = 0
                shared_comp_ready_cv.notify_all()

            # In the meantime, get new data
            if fill_array():
                total_read += n_items
            else:
                break

            print(f"{id_str} READ finished, num_comp_ready_shared_value={num_comp_ready_shared_value.value}")
            # Wait for comp processes to finish (could already be the case, doesn't matter)
            with shared_comp_ready_cv:
                shared_comp_ready_cv.wait_for(lambda: num_comp_ready_shared_value.value == total_comp_processes)
            print(f"{id_str} COMPs finished")
        print("Reader process instructed to end, switching continue var")
        continue_shared_value.value = False
        # Wait for all processes to have finished and notify them it's over
        with shared_alg_ready_cv:
            shared_alg_ready_cv.wait_for(lambda: num_alg_ready_shared_value.value == total_alg_processes)
            num_alg_ready_shared_value.value = 0
            shared_alg_ready_cv.notify_all()
        with shared_comp_ready_cv:
            shared_comp_ready_cv.wait_for(lambda: num_comp_ready_shared_value.value == total_comp_processes)
            num_comp_ready_shared_value.value = 0
            shared_comp_ready_cv.notify_all()


MAX_BUFFER_SIZE_BYTES = 1 * 1024 * 1024
PTCHANGE_DIR_NAME = 'ptchange'
OTHER_DATA_FN = "stats.csv"


def alg_producer(shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items,
                 shared_alg_ready_cv, num_alg_ready_shared_value,
                 continue_shared_value, alg_barrier,
                 alg, requested_sample_rate: int, change_queues,
                 own_data_save_dir, gt_ratio=-1):
    nn(shared_mem_accesses_list_name, shared_mem_access_type_list_name, n_items, shared_alg_ready_cv,
       num_alg_ready_shared_value,
       continue_shared_value, alg, requested_sample_rate, change_queues, own_data_save_dir, gt_ratio)
    assert continue_shared_value.value
    assert 0 < requested_sample_rate <= 1
    ptchange_dir = own_data_save_dir + PTCHANGE_DIR_NAME
    Path(ptchange_dir).mkdir(parents=False, exist_ok=False)
    ptchange_dir += '/'

    n_pfaults = curr_pfault_distance_sum = curr_non_pfault_dist_sum = 0
    seen = 0  # count the total considered and seen memory instructions
    id_str = f"{os.getpid()}:{threading.current_thread().ident} - "

    prev_tl, prev_page_base, repeat_count = [], 0, 0
    considered_loads, considered_stores = 0, 0

    def should_consider():
        return ((considered_loads + considered_stores) / seen) <= requested_sample_rate

    def should_consider_ratio():
        return should_consider() and ((is_load and ((considered_loads / considered_stores) <= gt_ratio))
                                      or (not is_load and (gt_ratio <= (considered_loads / considered_stores))))

    consideration_method = should_consider_ratio if gt_ratio >= 0 else should_consider
    shared_mem_accesses_list = shared_memory.SharedMemory(create=False, name=shared_mem_accesses_list_name)
    shared_mem_access_type_list = shared_memory.SharedMemory(create=False, name=shared_mem_access_type_list_name)
    mem_accesses_arr = np.ndarray(n_items, dtype=np.uintp, buffer=shared_mem_accesses_list.buf)
    mem_accesses_type_arr = np.ndarray(n_items, dtype=np.uint8, buffer=shared_mem_access_type_list.buf)
    print(f"{id_str} Waiting for first fill and starting...")
    n_writes = 0
    while True:
        # Wait to be notified you can go ; === value to be 0
        with shared_alg_ready_cv:
            shared_alg_ready_cv.wait_for(lambda: num_alg_ready_shared_value.value == 0)

        # Wait for all processes to be able to start to run, o/w/ DDLCK if one finishes before all have left the wait
        alg_barrier.wait()

        if not continue_shared_value.value:
            break

        ptchange = []
        for i in range(n_items):
            seen += 1
            if seen % 2_000_000 == 0:
                print(
                    f"{id_str} - Reached seen = {seen}\nSR={requested_sample_rate},#T={considered_loads + considered_stores} (#S={considered_stores},#L={considered_loads}{f', gt_ratio={gt_ratio}, curr_ratio={considered_loads / considered_stores}' if gt_ratio != -1 else ''}), n_writes={n_writes},i={i}")
            is_load = bool(mem_accesses_type_arr[i])
            mem_address = int(mem_accesses_arr[i])
            page_base = page_start_from_mem_address(mem_address)
            pfault = alg.is_page_fault(page_base)
            if pfault or consideration_method():
                if is_load:
                    considered_loads += 1
                else:
                    considered_stores += 1
                if repeat_count == 1:
                    # No need to consume : all algorithms have already correctly handled the page
                    # Also no need to update running averages, as md will be 0 if prev_tl = cur_tl
                    continue
                alg.consume(page_base)
                cur_tl = alg.get_temperature_list()
                # Update buffer
                for q in change_queues:
                    q.put((seen, cur_tl))
                md = overall_manhattan_distance(cur_tl, prev_tl, False)
                if pfault:
                    n_pfaults += 1
                    curr_pfault_distance_sum += md
                else:
                    curr_non_pfault_dist_sum += md
                ptchange.append((seen, md))
                prev_tl = cur_tl
                if prev_page_base == page_base:
                    repeat_count = 1
                else:
                    prev_page_base = page_base
                    repeat_count = 0

        # Save data to file:
        save_to_file_compressed(np.array(ptchange).transpose(), id_str, ptchange_dir, n_writes)
        ptchange = []
        n_writes += 1
        # Fill Qs with -1 so that they know it's the last item of the batch
        for q in change_queues:
            q.put((-1, prev_tl))

        # Say we're ready!
        with shared_alg_ready_cv:
            num_alg_ready_shared_value.value += 1
            shared_alg_ready_cv.notify_all()

    shared_mem_accesses_list.close()
    shared_mem_access_type_list.close()
    print(f"{id_str} Finished file reading")


DESC_STR = 'desc_string'
FIRST = "first"
BASELINE = "baseline"
BOTH = FIRST + BASELINE


def in_tra_ter_consumer(shared_comp_ready_cv, num_comp_ready_shared_value, continue_shared_value, comp_barrier,
                        producer_q_1, producer_q_baseline, savedir):
    nn(shared_comp_ready_cv, num_comp_ready_shared_value, continue_shared_value, producer_q_1, producer_q_baseline,
       savedir)
    assert continue_shared_value.value
    id_str = f"{os.getpid()}:{threading.get_native_id()}"
    (at, curr_alg_tl), (at_baseline, curr_baseline_tl) = (-1, []), (-1, [])
    to_poll = BOTH
    n_writes = 0
    print(f"{id_str} Waiting for first alg round and starting...")
    while True:
        # Wait to be notified you can go ; === value to be 0
        with shared_comp_ready_cv:
            shared_comp_ready_cv.wait_for(lambda: num_comp_ready_shared_value.value == 0)

        # Wait for all processes to be able to start to run, o/w/ DDLCK if one finishes before all have left the wait
        comp_barrier.wait()

        if not continue_shared_value.value:
            break

        alg_stop, baseline_stop = False, False
        diffs = []
        while True:
            assert FIRST in to_poll or BASELINE in to_poll
            if FIRST in to_poll:
                (at, curr_alg_tl) = producer_q_1.get()
                where = at
            if BASELINE in to_poll:
                (at_baseline, curr_baseline_tl) = producer_q_baseline.get()
                where = at_baseline
            if at == -1:
                alg_stop = True
            if at_baseline == -1:
                baseline_stop = True
            if alg_stop or baseline_stop:
                break
            if at == at_baseline:
                to_poll = BOTH
            else:
                if at < at_baseline:
                    to_poll = FIRST
                else:
                    to_poll = BASELINE
            diffs.append((where, overall_manhattan_distance(curr_alg_tl, curr_baseline_tl, True)))

        # Append the rest of the non-empty Q ; (else: stopped at the same time, no non-empty Qs)
        if not (baseline_stop and alg_stop):
            if baseline_stop:
                to_poll = FIRST
            else:
                to_poll = BASELINE
            while True:
                if FIRST in to_poll:
                    assert BASELINE not in to_poll
                    (at, curr_alg_tl) = producer_q_1.get()
                    where = at
                    other_at = at_baseline
                    other_to_poll = BASELINE
                else:
                    assert BASELINE in to_poll
                    (at_baseline, curr_baseline_tl) = producer_q_baseline.get()
                    where = at_baseline
                    other_at = at
                    other_to_poll = FIRST
                if where == -1:
                    break
                else:
                    approximation_distance = overall_manhattan_distance(curr_alg_tl, curr_baseline_tl, True)
                    diffs.append((where, approximation_distance))
                    if where > other_at:
                        to_poll = other_to_poll
                        break
                    elif where == other_at:
                        to_poll = BOTH
                        break
        to_poll = BOTH if (at == at_baseline) and alg_stop and baseline_stop else (
            BASELINE if at_baseline < at else FIRST)
        # Save data to file:
        save_to_file_compressed(np.array(diffs).transpose(), id_str, savedir, n_writes)
        diffs = []
        n_writes += 1

        # Say we're ready!
        with shared_comp_ready_cv:
            num_comp_ready_shared_value.value += 1
            shared_comp_ready_cv.notify_all()
    print(f"{id_str} Finished comparing")


def save_to_file_compressed(np_array, id_str, savedir, number_writes):
    print(id_str, f"Saving, n_writes={number_writes}")
    f = gzip.GzipFile(savedir + str(number_writes) + '.npy.gz', 'w')
    np.save(file=f, arr=np_array)
    f.close()


REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO = 0.01
AVERAGE_SAMPLE_RATIO = 0.05
N_ITEMS = 1024 * 1024 * 2


def main(args):
    K = 2
    MAX_PAGE_CACHE_SIZE = 507569  # pages = `$ ulimit -l`/4  ~= 2GB mem
    page_cache_size = MAX_PAGE_CACHE_SIZE // 32  # ~ 64 KB mem
    samples_div = [i / 100 for i in range(0, 101, 20) if i != 0]
    samples_div = [REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO, AVERAGE_SAMPLE_RATIO] + samples_div
    algs = [LRU_K(page_cache_size, K, 0, False), CLOCK(page_cache_size, K), ARC(page_cache_size), CAR(page_cache_size)]
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
    alg_to_queues = {}
    non_ratio_comparisons = []
    ratio_comparisons = []
    # 1) Standalone and ratio
    for alg in algs:
        for div in samples_div:
            if div != 1:
                compared_alg, baseline_alg_name = f"{alg.name()}_{str(div)}", f"{alg.name()}_1.0"
                non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg_name}")
                alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg, 0) + 1
                alg_to_queues[baseline_alg_name] = alg_to_queues.get(baseline_alg_name, 0) + 1
        # 1).ratio
        if args.ratio_realistic:
            compared_alg, baseline_alg_name = f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}_R", f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}"
            ratio_comparisons.append(f"{compared_alg} vs {baseline_alg_name}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg, 0) + 1
            alg_to_queues[baseline_alg_name] = alg_to_queues.get(baseline_alg_name, 0) + 1

    # 2)
    for alg1, alg2 in zip(algs[::2], algs[1::2]):
        for div in samples_div:
            compared_alg, baseline_alg_name = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg_name}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg, 0) + 1
            alg_to_queues[baseline_alg_name] = alg_to_queues.get(baseline_alg_name, 0) + 1

    # 3)
    for alg1, alg2 in zip(algs[:2], algs[2:]):
        for div in samples_div:
            compared_alg, baseline_alg_name = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg_name}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg, 0) + 1
            alg_to_queues[baseline_alg_name] = alg_to_queues.get(baseline_alg_name, 0) + 1

    assert 2 * (len(ratio_comparisons) + len(non_ratio_comparisons)) == sum(alg_to_queues.values())

    for k, v in alg_to_queues.items():
        alg_to_queues[k] = (0, [Queue(maxsize=-1) for _ in range(v)])

    base_dir = args.data_save_dir.resolve().as_posix() + '/'

    print("Creating algorithm processes")
    # Create all algorithm processes
    data_gathering_processes = []
    standalone_dir = Path(base_dir + "standalone")
    standalone_dir.mkdir(exist_ok=False, parents=False)
    standalone_dir_as_posix = standalone_dir.resolve().as_posix() + '/'

    num_alg_processes = len(algs) * (len(samples_div) + (1 if args.ratio_realistic else 0))
    num_comp_processes = len(ratio_comparisons) + len(non_ratio_comparisons)

    with SharedMemoryManager() as smm:
        base_arrays = [None, None]
        base_arrays[0] = np.zeros(N_ITEMS, dtype=np.uintp)
        base_arrays[1] = np.zeros(N_ITEMS, dtype=np.uint8)
        shared_mem = [None, None]
        copied_arrays = [None, None]
        cvs = [None, None]
        for i in range(len(base_arrays)):
            shared_mem[i] = smm.SharedMemory(size=base_arrays[i].nbytes)
            copied_arrays[i] = np.ndarray(base_arrays[i].shape, dtype=base_arrays[i].dtype, buffer=shared_mem[i].buf)
            copied_arrays[i][:] = base_arrays[i][:]
            cvs[i] = Condition()
        shared_values = [Value('B', lock=False), Value('B', lock=False)]
        shared_values[0].value = num_alg_processes
        shared_values[1].value = num_comp_processes
        shared_barriers = [Barrier(num_alg_processes), Barrier(num_comp_processes)]
        continue_shared_value = Value('B', lock=False)
        continue_shared_value.value = 1

        for alg in algs:
            for div in samples_div:
                alg_with_div = f"{alg.name()}_{str(div)}"
                alg_dir = Path(standalone_dir_as_posix + alg_with_div)
                alg_dir.mkdir(exist_ok=False, parents=False)
                q_list = alg_to_queues[alg_with_div][1]
                p = Process(target=alg_producer, args=(shared_mem[0].name, shared_mem[1].name, N_ITEMS, cvs[0],
                                                       shared_values[0], continue_shared_value, shared_barriers[0],
                                                       copy.deepcopy(alg), div, q_list,
                                                       alg_dir.resolve().as_posix() + '/'))
                data_gathering_processes.append(p)
            if args.ratio_realistic:
                ratio_div_alg_name = f"{alg.name()}_{str(REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO)}"
                ratio_alg = ratio_div_alg_name + "_R"
                alg_dir = Path(standalone_dir_as_posix + ratio_alg)
                alg_dir.mkdir(exist_ok=False, parents=False)
                data_gathering_processes.append(Process(target=alg_producer,
                                                        args=(shared_mem[0].name, shared_mem[1].name, N_ITEMS, cvs[0],
                                                              shared_values[0], continue_shared_value,
                                                              shared_barriers[0],
                                                              copy.deepcopy(alg),
                                                              REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO,
                                                              alg_to_queues[ratio_alg][1],
                                                              alg_dir.resolve().as_posix() + '/',
                                                              args.db[args.mem_trace_path.resolve().as_posix()][
                                                                  'ratio'])))

        print(f"Creating and starting {num_comp_processes} comparison processes")
        # Spawn all comparison processes
        comparison_processes = []

        comp_dir = Path(base_dir + "comp")
        comp_dir.mkdir(exist_ok=False, parents=False)
        comp_dir_as_posix = comp_dir.resolve().as_posix() + '/'

        for comparison in non_ratio_comparisons + ratio_comparisons:
            comp_alg_name, baseline_alg_name = tuple(comparison.split(" vs "))
            (comp_qhead, comp_qlist), (baseline_qhead, baseline_qlist) = alg_to_queues[comp_alg_name], alg_to_queues[
                baseline_alg_name]
            assert comp_qhead < len(comp_qlist) and baseline_qhead < len(baseline_qlist)
            comp_q, baseline_q = comp_qlist[comp_qhead], baseline_qlist[baseline_qhead]
            alg_to_queues[comp_alg_name] = (comp_qhead + 1, comp_qlist)
            alg_to_queues[baseline_alg_name] = (baseline_qhead + 1, baseline_qlist)
            save_dir = Path(comp_dir_as_posix + comp_alg_name + '_vs_' + baseline_alg_name)
            save_dir.mkdir(exist_ok=False, parents=False)
            process = Process(target=in_tra_ter_consumer,
                              args=(
                                  cvs[1], shared_values[1], continue_shared_value, shared_barriers[1],
                                  comp_q,
                                  baseline_q,
                                  save_dir.resolve().as_posix() + '/'
                              ))
            comparison_processes.append(process)
            process.start()

        # Start all algorithms
        print("Starting algorithm processes")
        for p in data_gathering_processes:
            p.start()

        r_process = Process(target=reader_process, args=(args.mem_trace_path.resolve().as_posix(),
                                                         shared_mem[0].name, shared_mem[1].name, N_ITEMS,
                                                         num_alg_processes, cvs[0], shared_values[0],
                                                         num_comp_processes, cvs[1], shared_values[1],
                                                         continue_shared_value))
        r_process.start()
        r_process.join()
        # Join processes and gather wb data
        print("Joining algorithm processes")
        for p in data_gathering_processes:
            p.join()

        for p in comparison_processes:
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
    # parser.add_argument('-g', '--make-graph', action=argparse.BooleanOptionalAction, default=False,
    #                     help="Generate graph from data. Data is alway saved")
    # parser.add_argument('--graph-save-type', type=str, default='png',
    #                     help="When -g is set, specifies the save type for the file. This option will be ignored if a "
    #                          "suffix is detected in the specified savefile. If 'pgf' the graph data will be optimised "
    #                          "for LaTeX.")
    # parser.add_argument('--graph-save-path', default="results/%%p_%%t",
    #                     help="Filename (and path) to the file where the data "
    #                          "will be saved. You can use %%p to denote the name "
    #                          "of the file specified by mem_trace_path as main "
    #                          "argument ; %%t for a premaid filename dpending on "
    #                          "the save state specified by -g (if set "
    #                          "'figure.png' else 'data.out'); %%%% to represent a "
    #                          "percentage sign.")
    parser.add_argument('--always-overwrite', default=False, action=argparse.BooleanOptionalAction,
                        help="Overwrite flag to always allow overwrite of save files")
    parser.add_argument("--data-save-dir", default="results/data/%mtp/%tst",
                        help="Directory where data is to be saved. Can use %%mtp to represent the benchmark name "
                             "of the memory traces; %%tst to represent a timestamp; %%%% to represent a"
                             "percentage sign.")

    parser.add_argument('mem_trace_path', type=str, help="Path to file containing the ground truth (PIN) memory trace")
    arg_ns = parser.parse_args()
    arg_ns.mem_trace_path = Path(arg_ns.mem_trace_path)
    if not arg_ns.mem_trace_path.exists() or not arg_ns.mem_trace_path.is_file():
        parser.error("Invalid mem_trace path: file does not exist!")
        exit(-1)

    # if "!" in arg_ns.graph_save_path:
    #     parser.error("Invalid graph save file path, '!' detected")
    #     exit(-1)
    # arg_ns.graph_save_path = Path(arg_ns.graph_save_path.replace("%%", "!")
    #                               .replace("!t",
    #                                        (f'figure{arg_ns.graph_save_type}' if arg_ns.make_graph else 'data.out'))
    #                               .replace("!p", arg_ns.mem_trace_path.stem)
    #                               .replace("!", "%"))
    # if arg_ns.graph_save_path.suffix != '':
    #     arg_ns.graph_save_type = arg_ns.graph_save_path.suffix
    # if arg_ns.graph_save_path.exists() and not arg_ns.always_overwrite:
    #     inp = 'n'
    #     while inp != 'y' or inp != '':
    #         inp = input(
    #             f"Overwrite was not specified, yet save file ({arg_ns.graph_save_path.resolve()}) already exists.\n"
    #             "Do you want to overwrite the file? [Y,n]").lower()
    #         if inp == 'n':
    #             print("Exiting...")
    #             exit(0)
    # arg_ns.graph_save_path.parent.mkdir(parents=True, exist_ok=True)
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
