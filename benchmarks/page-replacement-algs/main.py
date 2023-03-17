import json
from pathlib import Path

from algorithms.GenericAlgorithm import *
from algorithms.ARC import ARC
from algorithms.CAR import CAR
from algorithms.LRU_K import LRU_K
from algorithms.CLOCK import CLOCK
import argparse
from multiprocessing import Process, shared_memory, Barrier,Queue


def nn(*arguments):
    for arg in arguments:
        assert arg is not None


PTCHANGE = 'per_trace_change'
PFAULTS = 'pfaults'


def overall_manhattan_distance(tl1, baseline_tl, punish=False):
    sum_ = 0
    page_sorted_tl1 = sorted(list(enumerate(tl1)), key=lambda tpl: tpl[1].base)
    page_sorted_tl2 = sorted(list(enumerate(baseline_tl)), key=lambda tpl: tpl[1].base)

    def bsearch(page, slist):
        low = 0
        high = len(slist) - 1
        mid = 0
        while low <= high:
            mid = (high + low) // 2
            if slist[mid][1].base < page.base:
                low = mid + 1
            elif slist[mid][1].base > page.base:
                high = mid - 1
            else:
                return mid
        return -1

    for hotness, page in page_sorted_tl2:
        idx_in_tl1 = bsearch(page, page_sorted_tl1)
        if idx_in_tl1 != -1:
            sum_ += abs(hotness - page_sorted_tl2[idx_in_tl1][0])
        elif punish:
            sum_ += hotness
    return sum_


def alg_producer(file_path: str, alg, requested_sample_rate: int, change_queues,
                 own_data_shared_mem_basename, gt_ratio, is_ratio=False):
    assert 0 < requested_sample_rate <= 1
    data = {PTCHANGE: [], PFAULTS: 0}
    seen = 0  # count the total considered and seen memory instructions
    approximation_factor = 0.05
    with open(file_path, 'r') as mtf:
        prev_tl = []
        considered_loads, considered_stores = 0, 0

        def should_consider():
            return requested_sample_rate * (1 - approximation_factor) <= (
                    (considered_loads + considered_stores) / seen) <= requested_sample_rate * (
                    1 + approximation_factor)

        def should_consider_ratio():
            return should_consider() and (
                    (gt_ratio * (1 - approximation_factor) <= (considered_loads / considered_stores) and not is_load)
                    or (considered_loads / considered_stores) <= gt_ratio * (1 + approximation_factor) and is_load)

        consideration_method = should_consider if is_ratio else should_consider_ratio

        while (line := mtf.readline()) != '':
            seen += 1
            is_load = False
            if line[0] == 'R':
                is_load = True
            else:
                assert line[0] == 'W'
            mem_address = MemoryAddress(line[1:])
            associated_page = Page(page_start_from_mem_address(mem_address))
            pfault = alg.is_page_fault(associated_page)
            if pfault or consideration_method():
                if is_load:
                    considered_loads += 1
                else:
                    considered_stores += 1
                alg.consume(associated_page)
                cur_tl = alg.get_temperature_list()
                # Update buffer
                for q in change_queues:
                    q.put((seen, cur_tl))
                data[PTCHANGE].append((seen, overall_manhattan_distance(cur_tl, prev_tl, False)))
                if pfault:
                    data[PFAULTS] += 1
                prev_tl = cur_tl
        # Send finish signal
        for q in change_queues:
            q.put((-1, prev_tl))

        if own_data_shared_mem_basename is not None:
            wb = shared_memory.ShareableList(data[PTCHANGE], name=own_data_shared_mem_basename + '_' + PTCHANGE)
            wb.shm.close()
            wb = shared_memory.ShareableList(data[PFAULTS], name=own_data_shared_mem_basename + '_' + PFAULTS)
            wb.shm.close()


ALG_VARNAME = 'alg'
BASELINE_VARNAME = 'baseline'
DIFF_LIST = 'diffs'
DESC_STR = 'desc_string'


def in_tra_ter_consumer(alg1: str, baseline_alg: str, producer_q_1, producer_q_baseline, final_save_sm_base_name):
    alg_stop, baseline_stop = False, False
    (at, curr_alg_tl), (at_baseline, curr_baseline_tl) = producer_q_1.get(), producer_q_baseline.get()
    data = {ALG_VARNAME: alg1, BASELINE_VARNAME: baseline_alg, DIFF_LIST: []}
    to_select = []
    while True:
        if at == -1:
            alg_stop = True
        if at_baseline == -1:
            baseline_stop = True
        if alg_stop or baseline_stop:
            break
        if at == at_baseline:
            to_select = [producer_q_1, producer_q_baseline]
        else:
            if at < at_baseline:
                to_select = [producer_q_1]
                data[DIFF_LIST].append()
            else:
                to_select = [producer_q_baseline]
        data[DIFF_LIST].append((at, overall_manhattan_distance(curr_alg_tl, curr_baseline_tl, True)))
        for q in to_select:
            if q == producer_q_1:
                (at, curr_alg_tl) = producer_q_1.get()
            elif q == producer_q_baseline:
                (at_baseline, curr_baseline_tl) = producer_q_baseline.get()
    # Append the rest of the non-empty q
    if baseline_stop:
        assert producer_q_baseline.empty()
        q_to_empty = producer_q_1
    else:
        assert alg_stop
        q_to_empty = producer_q_baseline
    while True:
        somewhere, tl = q_to_empty.get()
        if somewhere == -1:
            break
        ad = overall_manhattan_distance(curr_alg_tl, tl, True) if alg_stop else \
            overall_manhattan_distance(tl, curr_baseline_tl, True)
        data[DIFF_LIST].append((somewhere, ad))

    if final_save_sm_base_name is not None:
        to_save = ('alg:' + alg1 + ';bs:' + baseline_alg)
        wb = shared_memory.SharedMemory(name=final_save_sm_base_name + '_' + DESC_STR, create=True,
                                        size=len(to_save.encode('utf-8')))
        wb.buf[:] = bytearray(to_save)
        wb.close()
        wb = shared_memory.ShareableList(data[DIFF_LIST], name=final_save_sm_base_name + '_' + DIFF_LIST)
        wb.shm.close()


REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO = 0.01
AVERAGE_SAMPLE_RATIO = 0.05
Q_MAX_SIZE = 1024

def main(args):
    K = 2
    MAX_PAGE_CACHE_SIZE = 507569  # pages = `$ ulimit -l`/4  ~= 2GB mem
    page_cache_size = MAX_PAGE_CACHE_SIZE // 16  # ~ 128 KB mem
    # We want:
    #   standalone alg w/ diff levels of mem traces --> (page faults + extra) ~=  TESTED_VALUE * full_memory_trace
    #                               where TESTED_VALUE is in {20%, 40%, 60%, 80%} ; vs 100% (full trace)
    #   intra-group alg w/ same level of mem trace; e.g.: LRU_40% vs CLOCK_40%
    #   inter-group alg w/ same level of mem traces; e.g.: CLOCK_60% vs CAR_60%
    #   if args.ratio is set, a tested value of 1% is also done for standalone, where ratio of mem accesses is preserved
    samples_div = [i / 100 for i in range(0, 101, 20) if i != 0]
    samples_div = [REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO, AVERAGE_SAMPLE_RATIO] + samples_div
    algs = [LRU_K(page_cache_size, K, 0, False), CLOCK(page_cache_size, K), ARC(page_cache_size), CAR(page_cache_size)]
    name_to_idx_mapping = dict([(alg.name(),idx) for (idx,alg) in enumerate(algs)])
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
                compared_alg,baseline_alg = f"{alg.name()}_{str(div)}", f"{alg.name()}_1.0"
                non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg}")
                alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg,0) + 1
                alg_to_queues[baseline_alg] = alg_to_queues.get(baseline_alg, 0) + 1
        # 1).ratio
        if args.ratio_realistic:
            compared_alg,baseline_alg = f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}_R", f"{alg.name()}_{REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO}"
            ratio_comparisons.append(f"{compared_alg} vs {baseline_alg}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg,0) + 1
            alg_to_queues[baseline_alg] = alg_to_queues.get(baseline_alg, 0) + 1

    # 2)
    for alg1, alg2 in zip(algs[::2], algs[1::2]):
        for div in samples_div:
            compared_alg,baseline_alg = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg,0) + 1
            alg_to_queues[baseline_alg] = alg_to_queues.get(baseline_alg, 0) + 1

    # 3)
    for alg1, alg2 in zip(algs[:2], algs[2:]):
        for div in samples_div:
            compared_alg,baseline_alg = f"{alg1.name()}_{str(div)}", f"{alg2.name()}_{str(div)}"
            non_ratio_comparisons.append(f"{compared_alg} vs {baseline_alg}")
            alg_to_queues[compared_alg] = alg_to_queues.get(compared_alg,0) + 1
            alg_to_queues[baseline_alg] = alg_to_queues.get(baseline_alg, 0) + 1

    assert len(ratio_comparisons)+len(non_ratio_comparisons) == sum(alg_to_queues.values())

    for k,v in alg_to_queues.items():
        alg_to_queues[k] = (0, [Queue(maxsize=Q_MAX_SIZE) for _ in range(v)])

    #Spawn all algorithm processes
    data_gathering_processes = []
    for alg in algs:
        for div in samples_div:
            alg_dict_name = f"{alg.name()}_{str(div)}"
            q_list = alg_to_queues[alg_dict_name][1]

            data_gathering_processes.append(Process(target=alg_producer,
                        args=()))

    # Spawn all comparison processes
    comparison_processes = []
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
    parser.add_argument('-g', '--make-graph', action=argparse.BooleanOptionalAction, default=False,
                        help="Generate graph from data. If set to false, data is saved in a format later usable to "
                             "generate the graphs")
    parser.add_argument('--save-type', type=str, default='png',
                        help="When -g is set, specifies the save type for the file. This option will be ignored if a "
                             "suffix is detected in the specified savefile. If 'pgf' the graph data will be optimised "
                             "for LaTeX.")
    parser.add_argument('--save-path', default="results/%%p_%%t", help="Filename (and path) to the file where the data "
                                                                       "will be saved. You can use %%p to denote the name "
                                                                       "of the file specified by mem_trace_path as main "
                                                                       "argument ; %%t for a premaid filename dpending on "
                                                                       "the save state specified by -g (if set "
                                                                       "'figure.png' else 'data.out'); %%%% to represent a "
                                                                       "percentage sign.")
    parser.add_argument('--always-overwrite', default=False, action=argparse.BooleanOptionalAction,
                        help="Overwrite flag to always allow overwrite of save files")
    parser.add_argument('mem_trace_path', type=str, help="Path to file containing the ground truth (PIN) memory trace")
    arg_ns = parser.parse_args()
    arg_ns.mem_trace_path = Path(arg_ns.mem_trace_path)
    if not arg_ns.mem_trace_path.exists() or not arg_ns.mem_trace_path.is_file():
        parser.error("Invalid mem_trace path: file does not exist!")
        exit(-1)
    if "!" in arg_ns.save_path:
        parser.error("Invalid save file path, '!' detected")
        exit(-1)
    arg_ns.save_path = Path(arg_ns.save_path.replace("%%", "!")
                            .replace("!t", (f'figure{arg_ns.save_type}' if arg_ns.make_graph else 'data.out'))
                            .replace("!p", arg_ns.mem_trace_path.stem)
                            .replace("!", "%"))
    if arg_ns.save_path.suffix != '':
        arg_ns.save_type = arg_ns.save_path.suffix
    if arg_ns.save_path.exists() and not arg_ns.always_overwrite:
        inp = 'n'
        ENTER_KEY = 10
        while inp.lower() != 'y' or inp != '':
            inp = input(f"Overwrite was not specified, yet save file ({arg_ns.save_path.resolve()}) already exists.\n"
                        "Do you want to overwrite the file? [Y,n]").lower()
            if inp == 'n':
                print("Exiting...")
                exit(0)
    arg_ns.save_path.parent.mkdir(parents=True, exist_ok=True)
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
