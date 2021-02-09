#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/injections.h>

#include "mem_pattern_trace.h"
#include "common.h"
#include "record.h"
#include "fetch.h"
#include "evict.h"
#include "fastswap_bench.h"

MODULE_AUTHOR("");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("");
static char *cmd, *val;
static int us_size = 2;

MODULE_PARM_DESC(cmd, "Command to properly change mem_trace system");
MODULE_PARM_DESC(val, "Value(usually 0 or 1) required for certain commands");
MODULE_PARM_DESC(us_size, "Microset size in pages (default is 2)");
module_param(cmd, charp, 0000);
module_param(val, charp, 0000);
module_param(us_size, int, 0000);

void mem_pattern_trace_start(int flags)
{
	pid_t pid = current->pid;
	const char *proc_name = current->comm;

	if (flags & TRACE_AUTO) {
		if (proc_trace_exists(proc_name))
			flags |= TRACE_PREFETCH;
		else
			flags |= TRACE_RECORD;
	}

	printk(KERN_INFO "%s%s%s for PROCESS with pid %d\n",
	       flags & TRACE_AUTO ? "AUTO-" : "",
	       flags & TRACE_RECORD ? "RECORDING" : "",
	       flags & TRACE_PREFETCH ? "PREFETCHING" : "", pid);

	if (flags & TRACE_RECORD) {
		BUG_ON(us_size < 2);
		record_init(pid, proc_name, current->mm, us_size);

	} else if (flags & TRACE_PREFETCH) {
		fetch_init(pid, proc_name, current->mm);
		evict_init();
	}
}

void mem_pattern_trace_end(int flags)
{
	// all _fini functions check whether they have been initialized
	// before performing any free-ing so no need to do it here
	evict_fini();
	fetch_fini();
	record_fini();
}

static void mem_pattern_trace_3(int flags)
{
	if (memtrace_getflag(TAPE_OPS) == 0) {
		printk(KERN_INFO "mem_pattern_trace: functionality is off\n");
		return;
	}

	printk(KERN_INFO "mem_pattern_trace called from pid %d with flags"
			 "[%s%s%s%s|%s%s%s]\n",
	       current->pid, flags & TRACE_START ? "TRACE_START" : "",
	       flags & TRACE_PAUSE ? "TRACE_PAUSE" : "",
	       flags & TRACE_RESUME ? "TRACE_RESUME" : "",
	       flags & TRACE_END ? "TRACE_END" : "",
	       flags & TRACE_RECORD ? "TRACE_RECORD" : "",
	       flags & TRACE_PREFETCH ? "TRACE_PREFETCH" : "",
	       flags & TRACE_AUTO ? "TRACE_AUTO" : ""

	       );

	// for use in miscellaneous experiments
	if (flags & TRACE_MISC) {
		fastswap_bench();
		return;
	}

	if (flags & TRACE_START) {
		record_force_clean();
		fetch_force_clean();
		mem_pattern_trace_start(flags);
		return;
	}

	if (flags & TRACE_END) {
		mem_pattern_trace_end(flags);
		return;
	}
	if (flags & KEVICTD_INIT) {
		//kevictd_init();
		return;
	}
	if (flags & KEVICTD_FINI) {
		//kevictd_fini();
		return;
	}
}

static void print_memtrace_flags()
{
	printk(KERN_INFO "memtrace global flags:\n"
			 "%-30s %d (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n",
	       // clang-format off
		"Microset size", us_size, "us_size",
		"Tape operations", memtrace_getflag(TAPE_OPS) ? "ON" : "OFF", "tape_ops",
		"Swap SSD Optim",  memtrace_getflag(SWAP_SSD_OPTIMIZATION) ? "ON" : "OFF", "ssdopt",
		"Fastswap writes",  memtrace_getflag(FASTSWAP_ASYNCWRITES) ? "ASYNC" : "SYNC", "async_writes",

		"Tape fetch",  memtrace_getflag(TAPE_FETCH) ? "ON" : "OFF", "tape_fetch",
		"print LRU dmesg logs",  memtrace_getflag(LRU_LOGS) ? "ON" : "OFF", "lru_logs"
	       // clang-format on
	       );
}

static void usage(void)
{
	printk(KERN_ERR "USAGE: insmod mem_pattern_trace.ko cmd=\"$cmd\" "
			"val=\"$val\" us_size=\"$us_size\"");
	printk(KERN_INFO "where $cmd is one of mem_trace cli commands (in "
			 "parentheses below)");
	printk(KERN_INFO "and $val is the value for commend (usualy 0 or 1) if "
			 "it requires a value\n");
	print_memtrace_flags();
}

static int __init leap_functionality_init(void)
{
	set_pointer(3, mem_pattern_trace_3);

	if (!cmd) {
		usage();
		return -1;
	}
#if DEBUG_FS
	debugfs_root = debugfs_create_dir("memtrace", NULL);
#endif

	if (strcmp(cmd, "tape_ops") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(TAPE_OPS) :
			      memtrace_clearflag(TAPE_OPS);
	} else if (strcmp(cmd, "ssdopt") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(SWAP_SSD_OPTIMIZATION) :
			      memtrace_clearflag(SWAP_SSD_OPTIMIZATION);
	} else if (strcmp(cmd, "async_writes") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(FASTSWAP_ASYNCWRITES) :
			      memtrace_clearflag(FASTSWAP_ASYNCWRITES);
	} else if (strcmp(cmd, "tape_fetch") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(TAPE_FETCH) :
			      memtrace_clearflag(TAPE_FETCH);
	} else if (strcmp(cmd, "lru_logs") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(LRU_LOGS) :
			      memtrace_clearflag(LRU_LOGS);
	} else {
		usage();
		return 0;
	}

	print_memtrace_flags();
	return 0;
}

static void __exit leap_functionality_exit(void)
{
	int i;

	printk(KERN_DEBUG "resetting injection points to noop");
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

#if DEBUG_FS
	debugfs_remove_recursive(debugfs_root);
#endif
	// free vmallocs and other state, in case
	// the process crashed or used syscalls incorrectly
	record_force_clean();
	fetch_force_clean();
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
