sudo perf record -v -e mem_inst_retired.all_stores:Pu,mem_inst_retired.all_loads:Pu -F max --strict-freq -d --kcore -N --timestamp-filename ../stream/stream
