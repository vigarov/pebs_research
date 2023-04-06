import argparse
import gzip
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt

def parse_args():
    parser = argparse.ArgumentParser(
        prog="ptrace_data",
        description="Plot script to show cache state modification patterns based on simulated (saved) data",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-o', '--output-dir', help='Output directory (backslash ended)', default="figures/")
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


COMP = "comp"
STANDALONE = "standalone"
PTCHANGE = "ptchange"
STATS = "stats.csv"

BUFFER_SIZE = 1024 * 1024

algs = ["lru","car","arc","car"]

def beautify(a:str):
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
    total_arr = np.zeros(BUFFER_SIZE * len(all_files)*4, dtype=np.uint64)
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
    fig,ax = plt.subplots()
    ax.plot(x,y)
    standalone = "vs" not in comp_or_standalone_name.lower()
    name_separated = [a.capitalize() for a in comp_or_standalone_name.split('_')]
    name_separated = [beautify(a) for a in name_separated]
    ns_tostr = ' '.join(name_separated)
    if standalone:
        title = "Impact of each considered access (pfault or not) for\n" + ns_tostr
    else:
        title = ns_tostr
    ax.set(xlabel="time (memory access #)",ylabel="overall distance",title=title)#,ylim=[-10,600])
    ax.grid()
    it = fig.get_size_inches()
    fig.set_size_inches((it[0]*5,it[1]))
    fig.show()
    #fig.savefig("a.png")
    print("test")


def main(args):
    p = Path(args.input_parent)
    assert p.exists()
    all_graphs = []
    all_raw_data_files = {}
    for subdir in p.iterdir():
        if subdir.is_dir():
            assert subdir.name == COMP or subdir.name == STANDALONE
            for data_parent_dir in subdir.iterdir():
                comp_or_standalone_name = data_parent_dir.name
                gzarr_dir = data_parent_dir.absolute().as_posix() + '/'
                if subdir.name == STANDALONE:
                    all_raw_data_files[comp_or_standalone_name] = gzarr_dir + STATS
                    gzarr_dir += PTCHANGE + '/'
                all_graphs.append(get_mem_impact_graph(comp_or_standalone_name, gzarr_dir))


# Press the green button in the gutter to run the script.
def set_mpl_rcparams(args):
    plt.rcParams.update({'figure.autolayout': True})


if __name__ == '__main__':
    args = parse_args()
    set_mpl_rcparams(args)
    main(args)

# See PyCharm help at https://www.jetbrains.com/help/pycharm/
