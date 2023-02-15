#! /bin/bash

for directory in *; do
    if [ -d "$directory" ]; then
        for filename in "$directory"/mem_trace*.log; do
            (trap 'kill 0' SIGINT; echo "#lines for `basename $directory` (`basename $filename`) : `wc -l $filename`" & echo "#Reads for `basename $directory` (`basename $filename`): `grep -c '^R' "$filename"`")
        done
    fi
done
