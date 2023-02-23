"""
Helper script to get `perf`'s PEBS memory trace of a program (using MEM_INST_RETIRED.ALL_STORES and
  MEM_INST_RETIRED.ALL_LOADS events in userspace only)
-n specifies number of runs of the program
By default, filters down to 4 traces:
    min.data - the trace with the minimum number of samples
    max.data - the trace with the maximum number of samples
    median.data - the trace corresponding to the median, based on the # samples
    mean.data - the trace closest to the mean of the # of samples
If -r is specified, adds an additional 4 traces, corresponding to the same filters, but using the R/W ratio instead of # samples
Similarly for -R (#reads), -W (#writes)
Simlinks are used to reference saved traces, such as to avoid duplication.
"""

import argparse
import math
from pathlib import Path
from subprocess import *
from datetime import datetime
import statistics
import itertools
import os
import shutil
import threading
import multiprocessing


def nn(*args):
    for arg in args:
        assert arg is not None


RAW_DATA_DIR = "raw_data"
COUNT_DATA_DIR = "count"
RATIO_DATA_DIR = "ratio"
READ_DATA_DIR = "read"
WRITE_DATA_DIR = "write"
EV_TYPE_POS = 4
EV_ADDR_POS = 5
EV_IP_POS = 13


class Event:
    def __init__(self, type_str: str, addr, ip):
        self.addr = addr
        self.ip = ip
        self.isLoad = "all_loads" in type_str


class Count:
    def __init__(self):
        self.count = 0

    def update(self, new_event):
        self.count += 1

    def __lt__(self, other):
        return self.count < other.count

    def __eq__(self, other):
        return self.count == other.count

    def __le__(self, other):
        return self < other or self == other

    def __gt__(self, other):
        return not (self <= other)

    def __ge__(self, other):
        return self > other or self == other

    def get(self):
        return self.count


class OpTypeTracker:
    def __init__(self, mode):
        assert mode == 'r' or mode == 'R' or mode == 'W'
        self.loads = 0
        self.stores = 0
        self.mode = mode

    def update(self, new_event):
        if new_event.isLoad:
            self.loads += 1
        else:
            self.stores += 1

    def __lt__(self, other):
        if self.mode == 'R':
            return self.loads < other.loads
        elif self.mode == 'W':
            return self.stores < other.stores
        else:
            return (self.loads / self.stores) < (other.loads / other.stores)

    def __eq__(self, other):
        if self.mode == 'R':
            return self.loads == other.loads
        elif self.mode == 'W':
            return self.stores == other.stores
        else:
            return (self.loads / self.stores) == (other.loads / other.stores)

    def __le__(self, other):
        return self < other or self == other

    def __gt__(self, other):
        return not (self <= other)

    def __ge__(self, other):
        return self > other or self == other

    def get(self):
        if self.mode == 'R':
            return self.loads
        elif self.mode == 'W':
            return self.stores
        else:
            return self.loads / self.stores


def rm_r(path):
    if not os.path.exists(path):
        return
    if os.path.isfile(path) or os.path.islink(path):
        os.unlink(path)
    else:
        shutil.rmtree(path)


def create_parent_directory(parent: str, overwrite_on_exist: bool):
    parent_path = Path(parent)
    if parent_path.exists():
        if not overwrite_on_exist:
            # Ask user if we want to overwrite
            ans = input(f"Parent directory ({parent_path}) already exists. Do you want to overwrite it? [y,n]")
            while ans != "y" and ans != "n":
                ans = input("Incorrect input. Please specify again: [y,n]")
            overwrite_on_exist = (ans == "y")
        if not overwrite_on_exist:
            exit(0)
        rm_r(parent_path.resolve().as_posix())

    # exist_ok=False in all below is a sanity check ; the if check above should be making sure it doesn't exist, or exit
    parent_path.mkdir(parents=True, exist_ok=False)
    return parent_path.resolve().as_posix()


def create_child_dir(parent_abs_path: str, freq: bool, value: int):
    child = Path(parent_abs_path + '/' + ('F_' if freq else 'c_') + str(value))
    child.mkdir(parents=False, exist_ok=False)  # shouldn't exist and parent should've already been created upon exec
    return child.resolve().as_posix()


def create_data_dir(parent_dir: str):
    parent_dir += '/'
    ret = Path(parent_dir + RAW_DATA_DIR)
    ret.mkdir(exist_ok=False, parents=False)
    return ret.resolve().as_posix()


def closest_to_mean(file_infos, values, option):
    mean = statistics.mean(values)
    corr_file, smallest_distance = list(file_infos.items())[0][0], abs(
        list(file_infos.items())[0][1][option].get() - mean)
    for k, v in file_infos.items():
        curr_dist = v[option].get() - mean
        if abs(curr_dist) < smallest_distance:
            smallest_distance = abs(curr_dist)
            corr_file = k
    return mean, smallest_distance, corr_file


def custom_dict_min(file_infos, values, option):
    min_value = min(values)
    return min_value, \
        [filename for filename, tracker_dict in file_infos.items() if tracker_dict[option].get() == min_value][0]


def custom_dict_max(file_infos, values, option):
    max_value = max(values)
    return max_value, \
        [filename for filename, tracker_dict in file_infos.items() if tracker_dict[option].get() == max_value][0]


def custom_dict_median(file_infos, values, option):
    median_value = sorted(values)[len(values)//2]
    return median_value, \
        [filename for filename, tracker_dict in file_infos.items() if tracker_dict[option].get() == median_value][0]


def remove_useless_files(kept_files, raw_data_dir):
    raw_data_path = Path(raw_data_dir)
    for posix_file_path in set(raw_data_path.glob("perf.data.*")) - set(raw_data_path.glob("perf.data.*.out")):
        posix_abs_path = posix_file_path.resolve().as_posix()
        if posix_abs_path not in kept_files:
            posix_file_path.unlink()
            # Also remove the .out of the corresponding data
            Path(posix_abs_path + ".out").unlink()


def create_output_files(kept_info, parent_dir):
    for option, (values_tuple, files_tuple) in kept_info.items():
        assert len(files_tuple) == 4
        assert len(values_tuple) == 6
        if option == 'c':
            subdir = COUNT_DATA_DIR
        elif option == 'r':
            subdir = RATIO_DATA_DIR
        elif option == 'R':
            subdir = READ_DATA_DIR
        else:
            assert option == 'W'
            subdir = WRITE_DATA_DIR
        symlink_parent_dir = parent_dir + subdir + '/'
        Path(symlink_parent_dir + 'min').symlink_to(files_tuple[0])
        Path(symlink_parent_dir + 'max').symlink_to(files_tuple[1])
        Path(symlink_parent_dir + 'mean').symlink_to(files_tuple[2])
        Path(symlink_parent_dir + 'median').symlink_to(files_tuple[3])
        with open(symlink_parent_dir + 'summary.txt', 'w') as summary_file:
            summary_file.write(
                f"Min: {values_tuple[0]}\nMax: {values_tuple[1]}\nMean: {values_tuple[2]} ; "
                f"distance of selected file: {values_tuple[3]}\nMedian: {values_tuple[4]}\n"
                f"Standard deviation: {values_tuple[5]}\n")


def analyse_run_data(parent_dir: str, raw_data_dir: str, count: bool, ratio: bool, read: bool, write: bool):
    assert count or ratio or read or write

    parent_dir += '/'

    if count:
        Path(parent_dir + COUNT_DATA_DIR).mkdir(exist_ok=False)
    if ratio:
        Path(parent_dir + RATIO_DATA_DIR).mkdir(exist_ok=False)
    if read:
        Path(parent_dir + READ_DATA_DIR).mkdir(exist_ok=False)
    if write:
        Path(parent_dir + WRITE_DATA_DIR).mkdir(exist_ok=False)
    data_dir_path = Path(raw_data_dir)
    all_data_files_paths = sorted(set(data_dir_path.glob("perf.data.*")) - set(data_dir_path.glob("perf.data.*.out")))
    file_infos = {}
    file_infos_lock = threading.Lock()
    # Split work into (# cpu cores) threads, to not have a memory overflow and premature kill since, either way, we
    # won't be able to execute more than (# cpu cores) threads in parallel. --> We want K list of size os.cpu_count()
    # --> K = ceil(len(all_data_files_paths) / os.cpu_count())
    for i in range(math.ceil(len(all_data_files_paths) / os.cpu_count())):
        sublist = all_data_files_paths[i::os.cpu_count()]
        all_threads = []
        for data_file_path in sublist:
            t = threading.Thread(target=analyse_data_file,
                                 args=(count, data_file_path, file_infos, file_infos_lock, ratio, read, write))
            all_threads.append(t)
            t.start()
        for thread in all_threads:
            thread.join()
    kept_info = {}
    options = []
    if count:
        options.append('c')
    if ratio:
        options.append('r')
    if read:
        options.append('R')
    if write:
        options.append('W')
    for option in options:
        values = [tracker_dict[option].get() for tracker_dict in file_infos.values()]
        min_value, min_file = custom_dict_min(file_infos, values, option)
        max_value, max_file = custom_dict_max(file_infos, values, option)
        mean_value, mean_distance, mean_file = closest_to_mean(file_infos, values, option)
        median_value, median_file = custom_dict_median(file_infos, values, option)
        kept_info[option] = (
            (min_value, max_value, mean_value, mean_distance, median_value, statistics.stdev(values)),
            (min_file, max_file, mean_file, median_file))

    remove_useless_files(set(itertools.chain.from_iterable([x[1] for x in kept_info.values()])), raw_data_dir)
    create_output_files(kept_info, parent_dir)


def analyse_data_file(count, data_file_path, file_infos_dict, dict_lock, ratio, read, write):
    base_analysis_cmd = "sudo perf script -i "
    abs_path = data_file_path.resolve().as_posix()
    file_info = {}
    if count:
        file_info['c'] = Count()
    if ratio:
        file_info['r'] = OpTypeTracker("r")
    if read:
        file_info['R'] = OpTypeTracker("R")
    if write:
        file_info['W'] = OpTypeTracker("W")
    analysis_cmd = base_analysis_cmd + abs_path
    events = run(analysis_cmd, stderr=STDOUT, stdout=PIPE, shell=True).stdout.decode('utf-8').splitlines()
    errors = 0
    for event_line in events:
        if "Warning" in event_line or "Processed" in event_line or "Check IO/CPU" in event_line:
            continue
        else:
            split_line = event_line.split()
            if len(split_line) == 0:
                continue
            try:
                event = Event(split_line[EV_TYPE_POS], split_line[EV_ADDR_POS], split_line[EV_IP_POS])
            except IndexError:
                errors += 1
                continue
            for f_i in file_info.values():
                f_i.update(event)
    if errors != 0:
        print(f"Got at least {errors} errors for {abs_path}")
    dict_lock.acquire()
    file_infos_dict[abs_path] = file_info
    dict_lock.release()


def get_pattern_iterator(pattern_str):
    class DefaultIterator:
        def __init__(self):
            self.at = 0

        def __iter__(self):
            return self

        def __next__(self):
            if self.at < 10:
                incr = 1
            elif self.at < 20:
                incr = 5
            elif self.at < 50:
                incr = 10
            elif self.at < 200:
                incr = 50
            elif self.at < 2000:
                incr = 500
            else:
                incr = 0
            if self.at == 200:
                self.at = 500
            elif self.at == 2000:
                self.at = 10000
            elif self.at >= 10000:
                raise StopIteration
            else:
                self.at += incr
            return self.at

    if pattern_str == "default":
        ret = DefaultIterator()
    elif pattern_str.startswith("linear:"):
        params: list = pattern_str.split(':')[1].split(',')
        assert len(params) == 3
        ret = range(params[0], params[1], params[2])
    elif pattern_str.startswith("custom:"):
        params: str = pattern_str.split(':')[1]
        if params[0] != '[' or params[len(params) - 1] != ']':
            raise ValueError
        else:
            import ast
            ret = [n.strip() for n in ast.literal_eval(params)]
    else:
        raise ValueError
    return ret


def parse_output_dir(args):
    nn(args, args.n, args.directory)
    args.directory = args.directory.replace('%N', str(args.n))
    args.directory = args.directory.replace('%%', '%')


def get_pebs(args):
    nn(args, args.program, args.n, args.count, args.ratio, args.read, args.write, args.overwrite, args.directory)
    if not args.count and not args.ratio and not args.read and not args.write:
        # user hasn't requested any measurements
        exit(-1)
    # Check program exists, and get its path
    program_path = Path(args.program)
    assert program_path.exists()
    parse_output_dir(args)

    parent_path_absolute = create_parent_directory(args.directory, args.overwrite)
    nn(parent_path_absolute)

    # Disable ASLR ; assumes disable_aslr alias in env
    run("echo 0 | sudo tee /proc/sys/kernel/randomize_va_space", shell=True)

    for step in iter(get_pattern_iterator(args.pattern)):
        print("Starting step", step)
        step_once(args, parent_path_absolute, program_path, step)


def step_once(args, parent_path_absolute, program_path, step):
    # 1. Get the data
    child_dir_abs = create_child_dir(parent_path_absolute, args.use_freq, step)
    raw_data_abs = create_data_dir(child_dir_abs) + '/'
    for _ in range(args.n):
        timestamp = datetime.now().strftime("%Y%m%d-%H%M%S%f")
        record_perf_output(program_path.resolve().as_posix(), ' '.join(args.program_arguments), raw_data_abs, step,
                           args.use_freq, timestamp)
    # 2. Analyse the data ; for a given run, all samples are in the raw_data_abs folder.
    # We should analyse data respective of the user args, create relevant symlinks in subfolders, remove unused data
    analyse_run_data(child_dir_abs, raw_data_abs, args.count, args.ratio, args.read, args.write)


def record_perf_output(program_absolute_path_str, program_args, raw_data_abs, step, freq, timestamp):
    # perf record -v -e mem_inst_retired.all_stores:Pu,mem_inst_retired.all_loads:Pu --strict-freq -d -N -c 1 -o <out_filename> <program> <program_arguments...>
    base_cmd = 'perf record -v -e mem_inst_retired.all_stores:Pu,mem_inst_retired.all_loads:Pu ' \
               '--strict-freq -d -N ' + ('-F' if freq else '-c') + ' '  # ,page-faults:u

    out_filename = str(raw_data_abs) + 'perf.data.' + timestamp
    cmd = base_cmd + str(step) + ' -o ' + out_filename + ' ' + program_absolute_path_str + ' ' + program_args
    out = run(cmd, shell=True, stdout=PIPE, stderr=STDOUT)
    # save output
    with open(out_filename + '.out', "w") as f:
        f.write(out.stdout.decode('utf-8'))


def parse_args():
    parser = argparse.ArgumentParser(
        prog="get_pebs_stats",
        description="Helper script to get `perf`'s PEBS memory trace of a program (using MEM_INST_RETIRED.ALL_STORES "
                    "and MEM_INST_RETIRED.ALL_LOADS events in userspace only)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-n', type=int, default=15, help="Specifies the number of times to run the program")
    parser.add_argument('-c', '--count', action=argparse.BooleanOptionalAction, default=True,
                        help="Saves traces based on event count")
    parser.add_argument('-r', '--ratio', action=argparse.BooleanOptionalAction, default=False,
                        help="Include ratio - Saves traces based on event ratio")
    parser.add_argument('-R', '--read', action=argparse.BooleanOptionalAction, default=False,
                        help="Include read - Saves traces based on reads (loads)")
    parser.add_argument('-W', '--write', action=argparse.BooleanOptionalAction, default=False,
                        help="Include write - Saves traces based on writes (stores)")
    parser.add_argument('-d', '--directory', help="Specifies in which directory to save the data. Use %%N to add the "
                                                  "value of N, %%%% for a percentage",
                        default=r'pebs_stats_n%N/' )
    parser.add_argument('--overwrite', action=argparse.BooleanOptionalAction, default=False,
                        help="Automatically overwrites directory (and data inside it) if the directory already exists")
    parser.add_argument('-F', '--use_freq', default=False, help="Use `-F` (frequency) option of perf instead of `-c` ("
                                                                "period)")

    parser.add_argument('--pattern', help="Specify the pattern in the following format: <pattern "
                                          "type>:<comma-separated type parameters>. Pattern type can be one of: "
                                          "linear,default,custom. If linear is chosen, the parameters are: start,"
                                          "max,step. If custom is chosen, the parameters are all the values that are "
                                          "to be tested. Default corresponds to linear(1,10,1) followed by linear(10,"
                                          "20,5) followed by linear(20,50,10) followed by linear(50,200,50) followed "
                                          "by custom([500,1000,1500,2000,10000]). It is meant to be used with --no-freq.",
                        default="default", type=str)
    parser.add_argument('program', type=str, help="program path")
    parser.add_argument('program_arguments', nargs=argparse.REMAINDER,
                        help="Arguments to pass to the program when executing it")
    return parser.parse_args()


def main():
    get_pebs(parse_args())


if __name__ == "__main__":
    main()
