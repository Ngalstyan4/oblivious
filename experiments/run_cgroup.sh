#!/bin/bash

FTRACE_FUNCTIONS=$(realpath ftrace_functions)
FTRACE_FUNCTIONS2=$(realpath ftrace_functions2)
gname="run_c"

function ftrace_begin {
    pushd /sys/kernel/debug/tracing
    echo 0 > function_profile_enabled
    echo 0 > options/function-trace
    echo "function" > current_tracer
    cat $FTRACE_FUNCTIONS > set_ftrace_filter
    cat $FTRACE_FUNCTIONS2 >> set_ftrace_filter
    echo 1 > function_profile_enabled
    echo 1 > tracing_on
    popd
}

function ftrace_end {
    pushd /sys/kernel/debug/tracing
    echo 0 > function_profile_enabled
    echo 0 > tracing_on
    echo "#### PROCESSOR 0 TRACE"
    cat trace_stat/function0

    echo "#### PROCESSOR 1 TRACE"
    cat trace_stat/function1
    popd
}

#ftrace_begin
echo "begin"
echo 0 > "/cgroup2/$gname/cgroup.procs"
#time taskset -c $1 ./mmult_eigen 4 4096 t
time $@
#ftrace_end
