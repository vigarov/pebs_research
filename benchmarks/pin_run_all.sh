#! /bin/bash

$PIN_PATH/pin -t atrace.so -o ./results/pmbench/mem_trace -- ./pmbench/pmbench -m 8192 -s 8192 -r 50 -d 0 -t rdtscp -c -n 1000
$PIN_PATH/pin -t atrace.so -o ./results/stream/mem_trace -- ./stream/stream