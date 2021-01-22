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

#include <linux/vmalloc.h>
#include <linux/frontswap.h>
#include <linux/pagemap.h>
#include <linux/delay.h>

MODULE_AUTHOR("");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("");
static char *cmd, *val;

MODULE_PARM_DESC(cmd, "Command to properly change mem_trace system");
MODULE_PARM_DESC(val, "Value(usually 0 or 1) required for certain commands");
module_param(cmd, charp, 0000);
module_param(val, charp, 0000);

void fastswap_bench()
{
	int i = 0;
	const int NUM_PAGES = 100;
	char *buf = vmalloc(4096 * NUM_PAGES);
	char *p = NULL;
	printk(KERN_INFO "Start fastswap write throughput benchmark\n");
	if (buf == NULL) {
		printk(KERN_ERR "unable to allocate buffer\n");
		return;
	}
	p = &buf[i * 4096];
	pte_t *pte = addr2pte((unsigned long)p, current->mm);
	for (i = 0; i < NUM_PAGES; i++) {
		struct page *mid_page = pte_page(*pte);
		pte++;
		if (__frontswap_store(mid_page) == 0) {
			set_page_writeback(mid_page);
			//unlock_page(mid_page);
		}
	}
	msleep(100);
	vfree(buf);
}

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
		record_init(pid, proc_name);

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
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n",
	       // clang-format off
		"Tape operations", memtrace_getflag(TAPE_OPS) ? "ON" : "OFF", "tape_ops",
		"Swap SSD Optim",  memtrace_getflag(SWAP_SSD_OPTIMIZATION) ? "ON" : "OFF", "ssdopt",
		"Fastswap writes",  memtrace_getflag(FASTSWAP_ASYNCWRITES) ? "ASYNC" : "SYNC", "async_writes",

		"Prefetch into custom buf",  memtrace_getflag(PAGE_BUFFER_ADD) ? "ON" : "OFF", "pagebuf_add",
		"Evict with custom buf",  memtrace_getflag(PAGE_BUFFER_EVICT) ? "ON" : "OFF", "pagebuf_evict"
	       // clang-format on
	       );
}

static void usage(void)
{
	printk(KERN_ERR "USAGE: insmod mem_pattern_trace.ko cmd=\"$cmd\" "
			"val=\"$val\"");
	printk(KERN_INFO "where $cmd is one of mem_trace cli commands (in "
			 "parentheses below)");
	printk(KERN_INFO "and $val is the value for commend (usualy 0 or 1) if "
			 "it requires a value\n");
	print_memtrace_flags();
}

static int __init leap_functionality_init(void)
{
	// set syscall entrypoint for prefetching
	set_pointer(3, mem_pattern_trace_3);

	if (!cmd) {
		usage();
		return -1;
	}

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
	} else if (strcmp(cmd, "pagebuf_add") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(PAGE_BUFFER_ADD) :
			      memtrace_clearflag(PAGE_BUFFER_ADD);
	} else if (strcmp(cmd, "pagebuf_evict") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(PAGE_BUFFER_EVICT) :
			      memtrace_clearflag(PAGE_BUFFER_EVICT);
	} else if (strcmp(cmd, "throughput_bench") == 0) {
		fastswap_bench();
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

	// free vmallocs and other state, in case
	// the process crashed or used syscalls incorrectly
	record_force_clean();
	fetch_force_clean();
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
