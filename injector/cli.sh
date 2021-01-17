# cmd=${1-"trace_init"}
cmd=${1-"help"}
val=${2-"invalid"}
sudo rmmod mem_pattern_trace.ko ;sudo insmod mem_pattern_trace.ko cmd="$cmd" val="$val"
