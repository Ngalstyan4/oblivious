# cmd=${1-"trace_init"}
cmd=${1-"help"}
proc=${2-"mmult_eigen"}
sudo rmmod mem_pattern_trace.ko ;sudo insmod mem_pattern_trace.ko cmd="$cmd" process_name="$proc"
