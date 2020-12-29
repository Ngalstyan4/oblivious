# cmd=${1-"trace_init"}
cmd=${1-"help"}
proc=${2-"mmult_eigen"}
sudo rmmod leap_functionality.ko ;sudo insmod leap_functionality.ko cmd="$cmd" process_name="$proc"
