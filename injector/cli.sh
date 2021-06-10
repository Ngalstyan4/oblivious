# cmd=${1-"trace_init"}
MOD="mem_pattern_trace"
cmd=${1-"help"}
val=${2-"invalid"}
SET_US_SIZE=""
if [[ $cmd == "us_size" ]]; then
	SET_US_SIZE="us_size=$val"
fi
sudo rmmod $MOD.ko ;sudo insmod $MOD.ko cmd="$cmd" val="$val" $SET_US_SIZE

# perf uses these in "extra" dir to extract symbols in `perf report -g`
# source` https://stackoverflow.com/questions/44326565/perf-kernel-module-symbols-not-showing-up-in-profiling
# sudo ln -sf `pwd`/$MOD.ko  /lib/modules/`uname -r`/extra/$MOD.ko

