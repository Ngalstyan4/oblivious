#!/bin/bash

RESULTS_DIR="experiment_results"
ALL_RATIOS="100 90 80 70 60 50 40 30 20 10"
APP_CPUS="1"

EXPERIMENT_NAME=${1}
EXPERIMENT_TYPE="" #"no_prefetching"|"linux_prefetching"|"tape_prefetching"
PROGRAM_REQUESTED_NUM_PAGES=${2} # 134244
PROGRAM_INVOCATION=${@:3} #"./mmult_eigen 44 4096 tt"

# variables holding the extracted csv tables
FTRACE_RESULTS_HEADER=""
FTRACE_RESULTS_ARR=()
CGROUP_RESULTS_HEADER=""
CGROUP_RESULTS_ARR=()
TIME_AND_SWAP_RESULTS_HEADER=""
TIME_AND_SWAP_RESULTS_ARR=()

function reset_results {
    # empty the arrays by deleting them
    unset FTRACE_RESULTS_ARR
    unset CGROUP_RESULTS_ARR
    unset TIME_AND_SWAP_RESULTS_ARR
}

FTRACE_FUNCTIONS=$(realpath ftrace_functions)
FTRACE_FUNCTIONS2=$(realpath ftrace_functions2)
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

BYTES_PER_MEMORY_PAGE=4096
# CGROUP constants
CGROUP_NAME='prefetch_ctrl'

function echoG() {
    printf "${GREEN} $1 ${NC} \n"
}

function echoR() {
    printf "${RED} $1 ${NC} \n"
}
function usage() {
    echo "sudo ./benchmark.sh experiment_name num_pages program_invocation"
    echo "e.g. sudo GOMP_CPU_AFFINITY="1-2" OMP_SCHEDULE=static OMP_NUM_THREADS=2 ./benchmark.sh test 134000 ./cpp/mmult_eigen_par 42 4096 mat"
    echo
    echo
}

function ftrace_begin {
    pushd /sys/kernel/debug/tracing
    echo 0 > function_profile_enabled
    echo nop > current_tracer
    cat $FTRACE_FUNCTIONS > set_ftrace_filter
    cat $FTRACE_FUNCTIONS2 >> set_ftrace_filter
    echo 1 > function_profile_enabled
    popd
}

function ftrace_end {
    pushd /sys/kernel/debug/tracing
    echo 0 > function_profile_enabled

    for p in $APP_CPUS
    do
	    PAGE_FAULT_HIT=$(cat trace_stat/function$p | grep __do_page_fault |awk -F' ' '{print $2}')
	    PAGE_FAULT_TIME=$(cat trace_stat/function$p | grep __do_page_fault |awk -F' ' '{print $3}')
	    PAGE_FAULT_S2=$(cat trace_stat/function$p | grep __do_page_fault |awk -F' ' '{print $7}')

	    SWAPIN_HIT=$(cat trace_stat/function$p | grep swapin_readahead | awk -F' ' '{print $2}')
	    SWAPIN_TIME=$(cat trace_stat/function$p | grep swapin_readahead | awk -F' ' '{print $3}')
	    SWAPIN_S2=$(cat trace_stat/function$p | grep swapin_readahead | awk -F' ' '{print $7}')

	    EVICT_HIT=$(cat trace_stat/function$p | grep try_to_free_mem_cgroup_pages | awk -F' ' '{print $2}')
	    EVICT_TIME=$(cat trace_stat/function$p | grep try_to_free_mem_cgroup_pages | awk -F' ' '{print $3}')
	    EVICT_S2=$(cat trace_stat/function$p | grep try_to_free_mem_cgroup_pages | awk -F' ' '{print $7}')

	    SYNC_TIME=$(cat trace_stat/function$p | grep fetch_page_fault_handler | awk -F' ' '{print $3}')

	    FTRACE_RESULTS_HEADER="RATIO,CPU,PAGE_FAULT_HIT,PAGE_FAULT_TIME,PAGE_FAULT_S2,SWAPIN_HIT,SWAPIN_TIME,SWAPIN_S2,EVICT_HIT,EVICT_TIME,EVICT_S2,SYNC_TIME"
	    FTRACE_RESULTS_ARR+=("$1,$p,$PAGE_FAULT_HIT,$PAGE_FAULT_TIME,$PAGE_FAULT_S2,$SWAPIN_HIT,$SWAPIN_TIME,$SWAPIN_S2,$EVICT_HIT,$EVICT_TIME,$EVICT_S2,$SYNC_TIME")

	    echoG "#### PROCESSOR $p TRACE"
	    cat trace_stat/function$p
    done
    popd
}

function cgroup_init {
   if [ ! -f "/cgroup2/cgroup.procs" ]; then
	mount -t cgroup2 nodev /cgroup2
	sh -c "echo '+memory' > /cgroup2/cgroup.subtree_control"
   fi
   rmdir "/cgroup2/$CGROUP_NAME"
   mkdir "/cgroup2/$CGROUP_NAME"
}

function cgroup_add {
    echo $1 > "/cgroup2/$CGROUP_NAME/cgroup.procs"
    # Q::chris: you set this in your setup script it it looks like this only
    # allowed/necessary when cgroup does not have any processes and has child cgroups
    # https://www.kernel.org/doc/html/v5.4/admin-guide/cgroup-v2.html
    # echo '+memory' > "/cgroup2/$CGROUP_NAME/cgroup.subtree_control"
}

function cgroup_limit_mem {
    echo $1 > "/cgroup2/$CGROUP_NAME/memory.high"
}

function cgroup_end {
    CG="/cgroup2/$CGROUP_NAME"
    NUM_MAJ_FAULT=$(cat "$CG/memory.stat" | grep "pgmajfault" | awk -F' ' '{print $2}')
    NUM_FAULT=$(cat "$CG/memory.stat" | grep "pgfault" | awk -F' ' '{print $2}')
    echoG "major fault: $NUM_MAJ_FAULT, minor fault: $((NUM_FAULT-NUM_MAJ_FAULT))"
    CGROUP_RESULTS_HEADER="RATIO,NUM_FAULTS,NUM_MAJOR_FAULTS"
    CGROUP_RESULTS_ARR+=("$1,$NUM_FAULT,$NUM_MAJ_FAULT")
    echo
    echo
}

# there is a weird vim-bash script highlighting issue. the subshell syntax confuses all of
# highlighting after this function
function run_experiment {
	for ratio in $@
	do
	    num_tapes=0
	    for tape in /data/traces/$EXPERIMENT_NAME/$ratio/*.tape.*
	    do
		    ((num_tapes+=1))
		    ln -sf "$tape" "/data/traces/$EXPERIMENT_NAME/`basename $tape`"
	    done
	    echoG "Begin experiment with ration ratio: $ratio\tneeded total pages: $PROGRAM_REQUESTED_NUM_PAGES (found $num_tapes tapes)"
	    cgroup_init
	    # need to run cgroup_add in a subshell to make sure all processes of cgroup exit before next iteration
	    # of the loop when cgroup_init tries to reset the cgroup
	    cgroup_limit_mem $(($ratio*$PROGRAM_REQUESTED_NUM_PAGES*$BYTES_PER_MEMORY_PAGE/100))
	    ftrace_begin
	    PAGES_SWAPPED_IN=$(cat "/sys/class/infiniband/mlx4_0/ports/1/counters/port_rcv_data")
	    PAGES_SWAPPED_OUT=$(cat "/sys/class/infiniband/mlx4_0/ports/1/counters/port_xmit_data")
	    RUN_TIME=$(
	    #subshell BEGIN
	    # \/ uncomment the line below if youd like to add the current process (INCLUDING THE BASH SHELL) to the cgroup
	    cgroup_add 0
	    # the pipe manipulation at the end of the line below swaps stdout and stderr so RUN_TIME variable
	    # will capture %U %S %E" but the program output wil be printed in terminal (as stderr though!!)
	    # ASSUMES THE PROGRAM RUN DOES NOT PRODUCE ANY STDERR
	    RUN_TIME=$((/usr/bin/time -f "%U,%S,%E" $PROGRAM_INVOCATION) 3>&2 2>&1 1>&3)
	    echo "$RUN_TIME" # becomes out of the subshell and is communicated back to the parent
	    #subshell END
	    )

	    PAGES_SWAPPED_IN=$((($(cat "/sys/class/infiniband/mlx4_0/ports/1/counters/port_rcv_data")-$PAGES_SWAPPED_IN) * 4 / 4096))
	    PAGES_SWAPPED_OUT=$((($(cat "/sys/class/infiniband/mlx4_0/ports/1/counters/port_xmit_data")-$PAGES_SWAPPED_OUT) * 4 / 4096))
	    TIME_AND_SWAP_RESULTS_HEADER="RATIO,USER,SYSTEM,WALLCLOCK,PAGES_EVICTED,PAGES_SWAPPED_IN"
	    TIME_AND_SWAP_RESULTS_ARR+=("$ratio,$RUN_TIME,$PAGES_SWAPPED_OUT,$PAGES_SWAPPED_IN")
	    ftrace_end $ratio
	    cgroup_end $ratio
	    echoG "Runtime: $RUN_TIME"

	done
}

function report_results {

    mkdir -p "$RESULTS_DIR/${EXPERIMENT_NAME}/$EXPERIMENT_TYPE"
    pushd "$RESULTS_DIR/${EXPERIMENT_NAME}/$EXPERIMENT_TYPE"

	    echo $FTRACE_RESULTS_HEADER > ftrace_results.csv
	    echoG $FTRACE_RESULTS_HEADER
	    for i in "${FTRACE_RESULTS_ARR[@]}"
	    do
		echo $i >> ftrace_results.csv
		echoG $i
	    done
	    echo

	    echo $CGROUP_RESULTS_HEADER > cgroup_results.csv
	    echoG $CGROUP_RESULTS_HEADER
	    for i in "${CGROUP_RESULTS_ARR[@]}"
	    do
		echo $i >> cgroup_results.csv
		echoG $i
	    done
	    echo

	    echo $TIME_AND_SWAP_RESULTS_HEADER > time_and_swap_results.csv
	    echoG $TIME_AND_SWAP_RESULTS_HEADER
	    for i in "${TIME_AND_SWAP_RESULTS_ARR[@]}"
	    do
		echo $i >> time_and_swap_results.csv
		echoG $i
	    done
    popd
}


##########################  CONFIRM EXPERIMENT BEFORE RUNNING  ########################################
if [ -z "$EXPERIMENT_NAME" ] || [ -z "$PROGRAM_INVOCATION" ] || [ -z "$PROGRAM_REQUESTED_NUM_PAGES" ]; then
    usage
    exit
fi

if [ "$EUID" -ne 0 ]; then
    echoR "Please run as root"
    usage
    exit
fi

echo "This script will run tracing in different C groups with different linux wap options"
echo "Experiment $EXPERIMENT_NAME with command \"$PROGRAM_INVOCATION\" (RSS=$PROGRAM_REQUESTED_NUM_PAGES ) "
read -p "^^ Is this correct? [y/n] " yn

if [[ $yn != "y" ]]; then
	echo "Aborted"
	exit
fi

#####################################  EXPERIMENTS BEGIN ########################################
EXPERIMENT_TYPE="no_prefetching"
 make sure tape prefetcher is not loaded
pushd ~/oblivious/injector
./cli.sh tape_ops 0
./cli.sh ssdopt 0
./cli.sh async_writes 0
popd

echoG ">>> Experiments with single-page swap-ins"
echo 0 > /proc/sys/vm/page-cluster
run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="linux_prefetching"
echoG ">>> Experiments with 8page swapins"
echo 3 > /proc/sys/vm/page-cluster
run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="linux_prefetching_asyncwrites"
pushd ~/oblivious/injector
./cli.sh async_writes 1
popd
echoG ">>> Experiments with 8page swapins, async writes"
echo 3 > /proc/sys/vm/page-cluster
run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="linux_prefetching_ssdopt"
echoG ">>> Experiments with swap write path SSD optimization"
pushd ~/oblivious/injector
./cli.sh tape_ops 0
./cli.sh ssdopt 1
./cli.sh async_writes 0
popd

echo 3 > /proc/sys/vm/page-cluster
run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="linux_prefetching_ssdopt_asyncwrites"
echoG ">>> Experiments with swap write path SSD optimization + async writes"
pushd ~/oblivious/injector
./cli.sh tape_ops 0
./cli.sh ssdopt 1
./cli.sh async_writes 1
popd
echo 3 > /proc/sys/vm/page-cluster
run_experiment $ALL_RATIOS

report_results
reset_results
################################  TAPE PREFETCHING EXPERIMENTS ##############################
# page cluster param below should not make a difference.
echo 0 > /proc/sys/vm/page-cluster
EXPERIMENT_TYPE="tape_prefetching_syncwrites"
echoG ">>> Experiments with tape prefetching"
pushd ~/oblivious/injector
./cli.sh tape_ops 1
./cli.sh ssdopt 0
./cli.sh async_writes 0
./cli.sh offload_fetch 0
./cli.sh unevictable 0
popd

run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="tape_prefetching_asyncwrites"
echoG ">>> Experiments with tape prefetching"
pushd ~/oblivious/injector
./cli.sh tape_ops 1
./cli.sh ssdopt 1
./cli.sh async_writes 1
./cli.sh offload_fetch 0
./cli.sh unevictable 0
popd

run_experiment $ALL_RATIOS

report_results
reset_results

EXPERIMENT_TYPE="tape_prefetching_asyncwrites_linux"
echoG ">>> Experiments with tape prefetching"
echo 3 > /proc/sys/vm/page-cluster
pushd ~/oblivious/injector
./cli.sh tape_ops 1
./cli.sh ssdopt 1
./cli.sh async_writes 1
./cli.sh offload_fetch 0
./cli.sh unevictable 0
popd

run_experiment $ALL_RATIOS

report_results
reset_results
echo 0 > /proc/sys/vm/page-cluster

EXPERIMENT_TYPE="tape_prefetching_asyncwrites_offload_fetch"
echoG ">>> Experiments with tape prefetching"
pushd ~/oblivious/injector
./cli.sh tape_ops 1
./cli.sh ssdopt 1
./cli.sh async_writes 1
./cli.sh offload_fetch 1
./cli.sh unevictable 1
popd

run_experiment $ALL_RATIOS

report_results
reset_results

