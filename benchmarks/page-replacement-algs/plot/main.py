import argparse
import csv
import gzip
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt


def parse_args():
    parser = argparse.ArgumentParser(
        prog="ptrace_data",
        description="Plot script to show cache state modification patterns based on simulated (saved) data",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-o', '--output-dir', help='Output directory (backslash ended)', default="graphs/")
    parser.add_argument('-p','--parent-all', help="Treat input_parent as parent directory for other parent "
                                                  "directories (recursively make graphs for all subdirs of parent "
                                                  "dir)",action=argparse.BooleanOptionalAction,default=False)
    parser.add_argument('--latex', help='Output for latex (pgf)', action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument('input_parent',
                        help="Path of the parent directory where the comparison and standalone args reside")
    # parser.add_argument('--graph-save-path', default="results/%%p_%%t",
    #                     help="Filename (and path) to the file where the data "
    #                          "will be saved. You can use %%p to denote the name "
    #                          "of the file specified by mem_trace_path as main "
    #                          "argument ; %%t for a premaid filename dpending on "
    #                          "the save state specified by -g (if set "
    #                          "'figure.png' else 'data.out'); %%%% to represent a "
    #                          "percentage sign.")
    return parser.parse_args()


RANDOM = "random"
FIFO = "fifo"
STATS = "stats.csv"

BUFFER_SIZE = 1024 * 1024

algs = ["lru", "clock","arc","car"]


def beautify(a: str):
    if a.lower() in algs:
        return a.upper()
    elif a == "R":
        return "ratio considered"
    else:
        return a


def get_mem_impact_graph(comp_or_standalone_name, gzarr_dir):
    p = Path(gzarr_dir)
    assert p.exists() and p.is_dir()
    all_files = list(p.iterdir())
    total_arr = np.zeros(BUFFER_SIZE * len(all_files) * 4, dtype=np.uint64)
    at = 0
    for array_file in all_files:
        f = gzip.open(array_file.absolute().as_posix(), "rb")
        file_content = f.read()
        f.close()
        size = len(file_content) // 8  # 8 == sizeof(uint64)
        total_arr[at:at + size] = np.frombuffer(file_content, dtype=np.uint64)
        at += size
    total_arr = total_arr[:at]
    x, y = total_arr[::2], total_arr[1::2]
    # Now P L O T
    fig, ax = plt.subplots()
    ax.plot(x, y)
    standalone = "vs" not in comp_or_standalone_name.lower()
    name_separated = [a.capitalize() for a in comp_or_standalone_name.split('_')]
    name_separated = [beautify(a) for a in name_separated]
    ns_tostr = ' '.join(name_separated)
    if standalone:
        title = "Impact of each considered access (pfault or not) for\n" + ns_tostr
    else:
        title = ns_tostr
    ax.set(xlabel="time (memory access #)", ylabel="overall distance", title=title)  # ,ylim=[-10,600])
    ax.grid()
    it = fig.get_size_inches()
    fig.set_size_inches((it[0] * 5, it[1]))
    fig.show()
    # fig.savefig("a.png")
    print("test")


def get_alg_info(dir_name):
    splitted = dir_name.split('_')
    return splitted[0], float(splitted[1])


PARSEC = "parsec"
PARSEC_BENCHES_AND_MINUS_VALUES = [("ferret", 54103303), ("canneal", 10012), ("dedup", 21828984),("bodytrack",1083792),("vips",11559423)]
PMBENCH, PM_MINUS_VALUE = "pmbench", 2080037
STREAM, STREAM_MINUS_VALUE = "stream", 117278
LINEWIDTHS = np.linspace(0.4,1.5,4)[::-1]
ALPHAS = np.linspace(0.7,1,4)[::-1]
MARKERSIZES = np.linspace(0.2,1,4)


def parse_memory_string(memory_string):
    stripped_string = memory_string.replace(" ", "")

    value = 0
    multiplier = 1

    try:
        value_string = stripped_string.rstrip("BKMGTP")
        unit_string = stripped_string[len(value_string):]
        value = int(value_string)
        unit_char = unit_string[0]

        if unit_char == 'B':
            multiplier = 1
        elif unit_char == 'K':
            multiplier = 1024
        elif unit_char == 'M':
            multiplier = 1024 * 1024
        elif unit_char == 'G':
            multiplier = 1024 * 1024 * 1024
        elif unit_char == 'T':
            multiplier = 1024 * 1024 * 1024 * 1024
        else:
            raise ValueError("Invalid memory unit: " + stripped_string)
    except ValueError:
        raise ValueError("Invalid memory string: " + stripped_string)

    return value * multiplier


def main(args):
    p = Path(args.input_parent)
    assert p.exists()
    p_abs_lower = p.absolute().as_posix().lower()
    bench_name = "unknown"
    y_minus_value = 0
    if PMBENCH in p_abs_lower:
        bench_name = PMBENCH
        y_minus_value = PM_MINUS_VALUE
    elif STREAM in p_abs_lower:
        bench_name = STREAM
        y_minus_value = STREAM_MINUS_VALUE
    elif PARSEC in p_abs_lower:
        bench_name = PARSEC + '/'
        for sub_bench, sub_minval in PARSEC_BENCHES_AND_MINUS_VALUES:
            if sub_bench in p_abs_lower:
                bench_name += sub_bench
                y_minus_value = sub_minval
                break
        if bench_name[-1] == '/':
            bench_name += "unknown"
    all_graphs = []
    CB_color_cycle = ['#377eb8', '#ff7f00', '#4daf4a',
                      '#f781bf', '#a65628', '#984ea3',
                      '#999999', '#e41a1c', '#dede00']
    num_pages = ""
    for f_r in p.iterdir():
        if f_r.is_dir():
            assert f_r.name == FIFO or f_r.name == RANDOM
            untracked_policy_name = FIFO if FIFO in f_r.name else RANDOM
            fig, axes = plt.subplots(2, 2)
            recap_axis_all, recap_axis_non_unique = axes[0][1], axes[1][1]
            alg_ax,alg_nu_ax = axes[0][0],axes[1][0]

            per_alg_data = {}

            for div_dir in f_r.iterdir():
                alg_name, div = get_alg_info(div_dir.name)
                assert alg_name.lower() in algs
                curr_dict = per_alg_data.get(alg_name,{})
                with open(div_dir.absolute().as_posix() + '/stats.csv') as csvfile:
                    reader = csv.DictReader(csvfile)
                    div_info_dict = {}
                    rows = 0
                    for row in reader:
                        div_info_dict = row
                        rows += 1
                    assert rows == 1
                    curr_dict[div] = div_info_dict
                per_alg_data[alg_name] = curr_dict

            sorted_per_alg_data = dict(sorted(per_alg_data.items(), key=lambda item: algs.index(item[0].lower())))
            width = 0.2
            multiplier = 0
            all_divs_str = None
            all_divs_placeholder = None

            for alg_name, divs_info_dict in sorted_per_alg_data.items():
                x_str, y = [], []
                for div, dict_div_info in divs_info_dict.items():
                    if round(float(div), 3) == 0.001 :
                        continue
                    x_str.append(str(round(float(div), 3)))
                    y.append(int(dict_div_info['pfaults']))
                    assert int(dict_div_info['considered_pfaults']) <= int(dict_div_info['pfaults'])
                    assert round((int(dict_div_info['considered_l']) + int(dict_div_info['considered_s']))/int(dict_div_info['seen']),3) == float(div)
                x_str, y = np.array(x_str), np.array(y)
                x_float = np.array([float(elem) for elem in x_str ])
                y_non_unique = y - y_minus_value
                x_sort_indices = x_float.argsort()
                x_str, y,x_float, y_non_unique = x_str[x_sort_indices], y[x_sort_indices],x_float[x_sort_indices], y_non_unique[x_sort_indices]
                x_placeholder = np.arange(len(x_str))
                if all_divs_str is None:
                    all_divs_str=x_str
                if all_divs_placeholder is None:
                    all_divs_placeholder=x_placeholder

                index = algs.index(alg_name.lower())
                color_hex = CB_color_cycle[index]
                color_rgb = [int(color_hex[1:][i:i+2], 16)/255 for i in (0, 2, 4)]
                color_rgba = tuple([*color_rgb,ALPHAS[index]])

                offset = width*multiplier
                alg_ax.bar(all_divs_placeholder + offset, y, width, color=color_hex,label=alg_name, hatch=''if f_r.name == FIFO else "/")
                alg_nu_ax.bar(all_divs_placeholder + offset, y_non_unique, width, color=color_hex, hatch=''if f_r.name == FIFO else "/")
                multiplier+=1

                recap_axis_all.plot(x_float, y, '-x', color=color_rgba,linewidth=LINEWIDTHS[index],markersize=MARKERSIZES[index]*10,zorder=2)
                recap_axis_non_unique.plot(x_float, y_non_unique, '-x', color=color_rgba,linewidth=LINEWIDTHS[index],markersize=MARKERSIZES[index]*10,zorder=2)

            fig.suptitle(f"{bench_name}, Memory size: {num_pages} pages; Workload requirement: {y_minus_value:,} unique pages, {untracked_policy_name.upper()}")
            fig.legend(loc='center right')

            full_ratio = parse_memory_string(num_pages)/y_minus_value
            if full_ratio <= 1:
                recap_axis_all.axvline(x=full_ratio, color=CB_color_cycle[-1],ls="--",zorder=1)
                recap_axis_non_unique.axvline(x=full_ratio, color=CB_color_cycle[-1],ls="--",zorder=1)

            alg_ax.set_xticks(all_divs_placeholder + 1.5*width, all_divs_str)
            alg_nu_ax.set_xticks(all_divs_placeholder + 1.5*width, all_divs_str)

            for ax in axes[0]:
                ax.set(ylabel="# pagefaults")
            for ax in axes[1]:
                ax.set(ylabel="# non-compulsory pagefaults")

            for i in range(len(axes)):
                y_lim_max =0
                for ax in axes[i]:
                    ax.set(xlabel='Percentage of mem. trace used as extra information')
                    ax_lims = ax.get_ylim()
                    y_lim_max = max(y_lim_max, ax_lims[1])
                # Hide x labels and tick labels for top plots and y ticks for right plots.
                for ax in axes[i]:
                    ax.set_ylim([0, y_lim_max])
                    ax.label_outer()

            curr_fig_size_inches = fig.get_size_inches()
            fig.set_size_inches((curr_fig_size_inches[0] * 2, curr_fig_size_inches[1]))

            fig.show()
            all_graphs.append((fig, untracked_policy_name.lower()))
        else:
            assert f_r.is_file() and f_r.name == "memory.txt"
            with open(f_r.absolute().as_posix(), 'r') as mem_info_file:
                mem_info = mem_info_file.readlines()
                assert len(mem_info) == 1
                splitted_mi = [string for string in mem_info[0].split('=') if "pages" in string]
                num_pages = splitted_mi[-1].split('pages')[0].replace("B","")

    save_parent = Path(args.output_dir+'/'+bench_name)
    save_parent.mkdir(parents=True,exist_ok=True)
    save_parent = Path(save_parent.absolute().as_posix()+'/'+str(num_pages)+'_pages')
    save_parent.mkdir(parents=False,exist_ok=False)
    save_par_abs_pos = save_parent.absolute().as_posix()
    with open(save_par_abs_pos+'/'+"input_dir.txt",'w') as f:
        f.write(p.absolute().as_posix())

    for fig,untracked_policy_name_lower in all_graphs:
        fig.savefig(save_par_abs_pos+'/'+untracked_policy_name_lower+'.png')


# Press the green button in the gutter to run the script.
def set_mpl_rcparams(args):
    plt.rcParams.update({'figure.autolayout': True})


if __name__ == '__main__':
    args = parse_args()
    set_mpl_rcparams(args)
    main(args)

# See PyCharm help at https://www.jetbrains.com/help/pycharm/
