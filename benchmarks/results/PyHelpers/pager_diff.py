import difflib
import argparse
import curses
import re


def nn(*args):
    for arg in args:
        assert arg is not None


START = "CURSES_COL_START_"
END = 'CURSES_COL_END'
GREEN_IDX = 1
RED_IDX = 2


def diff_strings(a: str, b: str) -> str:
    nn(a, b)
    output = []
    matcher = difflib.SequenceMatcher(None, a, b)
    green = START+str(GREEN_IDX)
    red = START+str(RED_IDX)

    for opcode, a0, a1, b0, b1 in matcher.get_opcodes():
        if opcode == 'equal':
            output.append(a[a0:a1])
        elif opcode == 'insert':
            output.append(f'{green}{b[b0:b1]}{END}')
        elif opcode == 'delete':
            output.append(f'{red}{a[a0:a1]}{END}')
        elif opcode == 'replace':
            output.append(f'{green}{b[b0:b1]}{END}')
            output.append(f'{red}{a[a0:a1]}{END}')
    return ''.join(output)


def add_colored_string(line,window):
    groups = re.split(r"("+START+r"\d.*?"+END+r")",line)
    for group in groups:
        matches = re.finditer(START+r"(\d{1})(.*?)"+END, group)
        match = list(matches)
        if len(match) != 0:
            assert len(match) == 1
            m = match[0]
            assert len(m.groups()) == 2
            # Color
            curses_color_pair_idx, text = m.groups(1)
            window.addstr(text, curses.color_pair(int(curses_color_pair_idx)))
        else:
            # No color
            window.addstr(group)


def print_initial_diff(f1, f2, n_lines: int, initial_bottom: int, offsets: list, crs_window):
    nn(f1, f2, n_lines, crs_window)
    assert n_lines > 1
    count = 0
    lines = []
    while count < n_lines and ((l1 := f1.readline()) != "") and ((l2 := f2.readline()) != ""):
        diff = diff_strings(l1, l2)
        to_append = str(count+initial_bottom+1)+":"+diff
        lines.append(to_append)
        offsets.append(f1.tell())
        add_colored_string(to_append, crs_window)
        count += 1
    crs_window.refresh()
    return count, lines, offsets


def handle_files(window, filename1: str, filename2: str, n_lines: int, full_size: bool = False, jump: bool = False):
    nn(window, filename1, filename2, n_lines)
    if not curses.has_colors():
        print("Terminal does not support colors, exiting.")
        return

    curses.start_color()
    curses.init_pair(GREEN_IDX, curses.COLOR_WHITE, curses.COLOR_GREEN)
    curses.init_pair(RED_IDX, curses.COLOR_WHITE, curses.COLOR_RED)
    if full_size:
        n_lines = curses.LINES-1
    else:
        n_lines = min(n_lines, curses.LINES-1)
    try:
        with open(filename1, 'r') as f1, open(filename2, 'r') as f2:
            offsets = [0]
            initial_bottom = 0
            if jump:
                while True:
                    l1, l2 = f1.readline(), f2.readline()
                    if l1 == "" or l2 == "":
                        print(f"Reached EOF! Files are identical{', but '+filename1+' is longer.' if l1!='' else ', but '+filename2+' is longer.' if l2!='' else '.'}")
                        return
                    if l1 == l2:
                        offsets.append(f1.tell())
                        initial_bottom += 1
                    else:
                        # Rewind to previous, and don't append offsets
                        o = offsets[initial_bottom]
                        nn(o)
                        f1.seek(o)
                        f2.seek(o)
                        break

            bottom, lines, offsets = print_initial_diff(f1, f2, n_lines, initial_bottom, offsets, window)
            nn(bottom, lines)
            assert bottom <= n_lines
            if bottom < n_lines:
                # we've reached EOF of one of the files, print (n_lines - bottom) of the other
                eof_line = f1.readline()

                def print_rest_o_f(other_file, eof_filename, other_filename):
                    print(f"{eof_filename} ended. To fill the window, we will use the lines of {other_filename}")
                    rest = [next(other_file) for _ in range(n_lines - bottom)]
                    for line in rest:
                        # No need to update lines or offsets, as we're exiting afterwards
                        add_colored_string(line, window)
                    window.refresh()

                if eof_line == "":
                    print_rest_o_f(f2, filename1, filename2)
                else:
                    add_colored_string(eof_line, window)
                    bottom += 1
                    print_rest_o_f(f1, filename2, filename1)
                print("EOF reached, exiting...")
                return
            bottom+=initial_bottom
            window.keypad(True)
            at_eof = False

            def advance_once(at_eof_, bottom_):
                if not at_eof_:
                    del lines[0]
                    prev_offset_: int = offsets[bottom_]
                    bottom_ += 1
                    nn(prev_offset_)
                    f1.seek(prev_offset_)
                    f2.seek(prev_offset_)
                    l1_, l2_ = f1.readline(), f2.readline()
                    if l1_ == "" and l2_ == "":
                        at_eof_ = True
                        lines.append("Reached EOF for both files")
                    elif l1_ == "":
                        at_eof_ = True
                        lines.append(f"Reached EOF for {filename1}. {filename2}'s next line is {l2_}")
                    elif l2_ == "":
                        at_eof_ = True
                        lines.append(f"Reached EOF for {filename2}. {filename1}'s next line is {l1_}")
                    else:
                        diff_ = diff_strings(l1_, l2_)
                        offsets.append(f1.tell())
                        lines.append(str(bottom_) + ":" + diff_)
                else:
                    lines[len(lines) - 1] = "Already reached EOF, no changes." + lines[len(lines) - 1]
                return bottom_, at_eof_

            while True:
                c = window.getch()
                if c == ord('q'):
                    # Quit
                    break
                elif c == ord('j'):
                    # Go Down
                    bottom, at_eof = advance_once(at_eof, bottom)
                elif c == ord('k'):
                    if bottom - n_lines != 0:
                        del lines[len(lines) - 1]
                        bottom -= 1
                        at_eof = False
                        prev_offset: int = offsets[bottom - n_lines]
                        nn(prev_offset)
                        f1.seek(prev_offset)
                        f2.seek(prev_offset)
                        l1, l2 = f1.readline(), f2.readline()
                        assert l1 != "" and l2 != ""
                        diff = diff_strings(l1, l2)
                        lines.insert(0, str(bottom-n_lines+1)+":"+diff)
                elif c == ord(' '):
                    for _ in range(n_lines):
                        bottom, at_eof = advance_once(at_eof, bottom)
                elif c == ord('s'):
                    # Skip N lines ; N is the next-input
                    inp_numbers = []
                    inp = window.getch()
                    while inp != curses.KEY_ENTER and inp != 10:  # 10 is Enter
                        if ord('0') <= inp <= ord('9'):
                            inp_numbers.append(inp-ord('0'))
                        elif inp == curses.KEY_BACKSPACE:
                            # Backspace
                            if len(inp_numbers) != 0:
                                del inp_numbers[len(inp_numbers)-1]
                        inp = window.getch()
                    N = 0
                    inp_numbers.reverse()
                    for exponent, number in enumerate(inp_numbers):
                        N += number * (10**exponent)
                    # skip
                    for _ in range(N):
                        bottom, at_eof = advance_once(at_eof, bottom)

                window.erase()
                for line in lines:
                    add_colored_string(line, window)
                window.refresh()

    except IOError as e:
        print(f"IO Error. {e}")
    except curses.error as e:
        print(f"Curses terminal error {e}")


def parse_args():
    parser = argparse.ArgumentParser(
        prog='pager_diff',
        description='This program `diff`s specified files in a pager (`less`-like) manner. Files must be text-based',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-w', '--window-size', type=int,
                        help="When -f is not specified, sets the size of the window in lines to be displayed."
                             "This will be truncated depending on the size of your terminal", default=5)
    parser.add_argument('-f', '--full_size', help="Use full console height. If this is specified, -w is ignored.",action=argparse.BooleanOptionalAction)
    parser.add_argument('-j','--jump',help="Jump to the first line that's different",action=argparse.BooleanOptionalAction)
    parser.add_argument('file1', type=str, help='The first file to be compared')
    parser.add_argument('file2', type=str, help='The second file to be compared')
    return parser.parse_args()


def main():
    args_namespace = parse_args()
    curses.wrapper(handle_files, args_namespace.file1, args_namespace.file2, args_namespace.window_size,
                   args_namespace.full_size, args_namespace.jump)


if __name__ == '__main__':
    main()
