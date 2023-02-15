sudo perf stat -r 5 -v -e mem_inst_retired.all_stores:Pu,mem_inst_retired.all_loads:Pu ../stream/stream
