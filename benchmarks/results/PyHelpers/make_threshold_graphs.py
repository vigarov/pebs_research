import argparse
from pathlib import Path
import numpy as np
import matplotlib as mpl
import matplotlib.pyplot as plt


def parse_args():
    parser = argparse.ArgumentParser(
        prog="plot_pebs_stats",
        description="Helper script to plot the stats generated using get_pebs_stats.py script",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--raw-data-dir-name', help="Name of the raw data directory", default="raw_data")
    parser.add_argument('--summary-fn', help="Name of the summary file", default="summary.txt")
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
    parent_path = Path(args.directory)
    if not parent_path.exists():
        print("Error: specified path doesn't exist")
        exit(-1)
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
    return uses_count, all_data


def generate_figure(use_count, data_dict: dict):
    if len(data_dict.values()) != 4:
        print("Horizontal unimplemented yet")
        exit(1)
    fig, axs = plt.subplots(2, 2)
    fig.suptitle(("Count" if use_count else "Frequency") + " for different metrics")
    at_x_y = 0
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
        curr_ax = axs[at_x_y & 1, (at_x_y & 2) >> 1]
        curr_ax.set_xticks(np.arange(len(thresholds)))
        curr_ax.set_xticklabels(thresholds)
        curr_ax.errorbar(np.arange(len(thresholds)), means, stddevs, fmt='or', lw=3)
        curr_ax.errorbar(np.arange(len(thresholds)), means, [means - mins, maxes - means], fmt='.k', lw=1,
                         ecolor='gray')
        curr_ax.plot(np.arange(len(thresholds)), medians, 'xg')
        curr_ax.set_title(str(metric))
        at_x_y += 1
    for ax in axs.flat:
        ax.set(xlabel="Thresholds")
    for ax in axs.flat:
        ax.label_outer()
    return fig


def main():
    args = parse_args()
    count, data = get_data(args)
    fig = generate_figure(count, data)
    fig.show()


if __name__ == "__main__":
    main()
