import argparse
from pathlib import Path
import numpy as np
import matplotlib.ticker
import matplotlib.pyplot as plt

STREAM_MODE = "stream"
PMBENCH_MODE = "pmbench"


def parse_args():
    parser = argparse.ArgumentParser(
        prog="plot_pebs_stats",
        description="Helper script to plot the stats generated using get_pebs_stats.py script",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--raw-data-dir-name', help="Name of the raw data directory", default="raw_data")
    parser.add_argument('--summary-fn', help="Name of the summary file", default="summary.txt")
    parser.add_argument('-m', '--mode',
                        help="Specify whether the benchmark is STREAM or PMBENCH. Auto will try to find "
                             "out by looking for hints in the direcotry name.", default='auto')
    parser.add_argument('-o', '--output-dir', help='Output directory (backslash ended)', default="figures/")
    parser.add_argument('directory', help="Parent directory where the subdirectory samples start",
                        default='pebs_stats/')
    return parser.parse_args()


def parse_summary_file(summary_file):
    data = []
    with open(summary_file.resolve().as_posix(), 'r') as f:
        for line in f.readlines():
            # Order is min,max,mean,median,stddev
            data.append(round(float(line.split(': ')[1].split(' ;')[0]), 4))
    return data


def update_or_add(dict_, key, value):
    curr = dict_.get(key)
    if curr is None:
        curr = [value]
    else:
        curr.append(value)
    dict_[key] = curr


def get_data(args):
    if args.mode == "auto":
        if STREAM_MODE in args.directory.lower():
            args.mode = STREAM_MODE
        elif PMBENCH_MODE in args.directory.lower():
            args.mode = PMBENCH_MODE
        else:
            print("Couldn't automatically find mode; please specify it explicitly.")
            exit(1)
    parent_path = Path(args.directory)
    if not parent_path.exists():
        print("Error: specified path doesn't exist")
        exit(-1)
    n_index = args.directory.find('_n')
    if n_index == -1:
        n_value = 'unknown'
    else:
        n_value = args.directory[n_index + len('_n'):].replace('/', '')
    uses_count = None
    all_data = {}
    for sampled_subdirectory in parent_path.iterdir():
        if not sampled_subdirectory.is_dir():
            continue
        if uses_count is None:
            assert sampled_subdirectory.name != ''
            uses_count = (sampled_subdirectory.name[0] == 'c')
        sample_value = int(sampled_subdirectory.name.split('_')[1])
        for metric_subdirectory in sampled_subdirectory.iterdir():
            if metric_subdirectory.name != args.raw_data_dir_name:
                # Get summary.txt
                metric_subdirectory_abs = metric_subdirectory.resolve().as_posix()
                summary_file = Path(metric_subdirectory_abs + '/' + args.summary_fn)
                if not summary_file.exists():
                    print(f"Could not find summary file in parent directory {metric_subdirectory_abs}")
                    exit(1)
                # Parse it
                data: list = parse_summary_file(summary_file)
                update_or_add(all_data, metric_subdirectory.name, (sample_value, data))
    return parent_path.resolve().as_posix(), uses_count, n_value, all_data


# Using get_stats.sh, we get (more or less)
OPT_READS_STREAM = 540053070
OPT_COUNT_STREAM = 860076507
OPT_COUNT_PMBENCH = 522400470
OPT_READS_PMBENCH = 340044031


def generate_figure(use_period, data_dict: dict, mode: str, n: str):
    if mode == STREAM_MODE:
        opt_reads = OPT_READS_STREAM
        opt_count = OPT_COUNT_STREAM
    else:
        assert mode == PMBENCH_MODE
        opt_reads = OPT_READS_PMBENCH
        opt_count = OPT_COUNT_PMBENCH
    opt_writes = opt_count - opt_reads
    opt_ratio = float(opt_reads / opt_writes)
    plt.rcParams.update({'figure.autolayout': True})
    fig, axs = plt.subplots(len(data_dict.values()), figsize=(12, 10))
    fig.suptitle(
        f"{'Period' if use_period else 'Frequency'} thresholds comparison using different metrics\nN={n} runs of {mode} benchmark per threshold")
    at = 0
    for metric, data_list in data_dict.items():
        thresholds = np.array([tuple_[0] for tuple_ in data_list])
        sorted_idx_in_order = np.argsort(thresholds)
        thresholds = thresholds[sorted_idx_in_order]
        # Order is min,max,mean,median,stddev
        mins = np.array([tuple_[1][0] for tuple_ in data_list])[sorted_idx_in_order]
        maxes = np.array([tuple_[1][1] for tuple_ in data_list])[sorted_idx_in_order]
        means = np.array([tuple_[1][2] for tuple_ in data_list])[sorted_idx_in_order]
        medians = np.array([tuple_[1][3] for tuple_ in data_list])[sorted_idx_in_order]
        stddevs = np.array([tuple_[1][4] for tuple_ in data_list])[sorted_idx_in_order]
        curr_ax = axs[at]
        curr_ax.set_xticks(np.arange(len(thresholds)))
        curr_ax.set_xticklabels(thresholds)
        stddev_error = curr_ax.errorbar(np.arange(len(thresholds)), means, stddevs, fmt='or', lw=3, capsize=5, zorder=1)
        min_max_error = curr_ax.errorbar(np.arange(len(thresholds)), means, [means - mins, maxes - means], capsize=3,
                                         fmt='.k', lw=1, ecolor='gray', zorder=3)
        median_green_crosses = curr_ax.scatter(np.arange(len(thresholds)), medians, facecolor='g', marker='x', zorder=2)
        curr_ax.set_title(str(metric).capitalize())
        y_max = None
        y_min = None
        scaled_opt = None
        if metric == "ratio":
            y_max = 10
            y_min = -1
            blue_line = curr_ax.axhline(y=opt_ratio, color='b', linestyle='-')
            curr_ax.set_yticks(list(curr_ax.get_yticks()) + [opt_ratio, y_min, y_max])
            curr_ax.set_ylabel("Load/Store Ratio", ha='left', y=1.1, rotation=0, labelpad=0)
            curr_ax.get_yaxis().set_major_formatter(matplotlib.ticker.FormatStrFormatter('%.2f'))
        elif metric == "read":
            scaled_opt = opt_reads / 1000
            curr_ax.set_ylabel('# of loads', ha='left', y=1.1, rotation=0, labelpad=0)
        elif metric == "write":
            scaled_opt = opt_writes / 1000
            curr_ax.set_ylabel('# of stores', ha='left', y=1.1, rotation=0, labelpad=0)
        else:
            assert metric == "count"
            scaled_opt = opt_count / 1000
            curr_ax.set_ylabel('Total # of memory instructions (R+W)', ha='left', y=1.1, rotation=0, labelpad=0)
        if metric != "ratio":
            assert scaled_opt is not None
            if mode == PMBENCH_MODE:
                less_scaled_opt = scaled_opt * 10
            yellow_line = curr_ax.axhline(y=scaled_opt, color='y', linestyle='-')
            if mode == PMBENCH_MODE:
                green_line = curr_ax.axhline(y=less_scaled_opt, color='g', linestyle='-')
            y_min = 0
            curr_ax.set_yticks(
                list(curr_ax.get_yticks()) + ([scaled_opt] if mode == STREAM_MODE else [scaled_opt, less_scaled_opt]))
        curr_ax.set_ylim([y_min, y_max])
        c_ticks = list(curr_ax.get_yticks())
        # Show axis limits
        for y_val in curr_ax.get_ylim():
            if y_val not in c_ticks:
                curr_ax.set_yticks(list(curr_ax.get_yticks()) + [y_val])
        # Show if mean is outside of range
        (lim_d, lim_u) = curr_ax.get_ylim()
        for idx, mean in enumerate(means):
            y_lim = None
            if mean > lim_u:
                y_lim = lim_u
                character = '^'
            elif mean < lim_d:
                y_lim = lim_d
                character = 'v'
            if y_lim is not None:
                assert character is not None
                curr_ax.plot([idx], [y_lim], f'{character}k', zorder=10, clip_on=False)
        at += 1
    # Set legend ; using last lines is fine
    legend_tuple = [stddev_error, min_max_error, median_green_crosses, blue_line, yellow_line]
    legend_desc = ['Standard deviation', "Black point: mean; bounded by min&max", "Median", "Ground Truth value",
                   "(Ground Truth value)/1000 (0.1%)"]
    if mode == PMBENCH_MODE:
        legend_tuple.append(green_line)
        legend_desc.append("(Ground Truth value)/100 (1%)")
    legend_tuple = tuple(legend_tuple)
    legend_desc = tuple(legend_desc)
    fig.legend(legend_tuple, legend_desc, loc="upper right")
    for ax in axs.flat:
        ax.set(xlabel="Period Thresholds")
    return fig


def main():
    args = parse_args()
    parent_path, count, n, data = get_data(args)
    fig = generate_figure(count, data, args.mode, n)
    fig.show()
    # fig.savefig(parent_path + '/stats.png')
    output_path = Path(args.output_dir).resolve()
    assert output_path.is_dir()
    if not output_path.exists():
        output_path.mkdir()
    fig.savefig(f"{output_path.as_posix()}/{args.mode}_n{n}_stats.png")


if __name__ == "__main__":
    main()
