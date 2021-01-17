#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/fs.h> // Needed by filp
#include <asm/uaccess.h> // Needed by segment descriptors
#include <asm/tlbflush.h>

#include <linux/injections.h>

#include "mem_pattern_trace.h"
#include "common.h"
#include "page_buffer.h"
#include "record.h"
#include "fetch.h"
#include "evict.h"
#include "kevictd.h"

MODULE_AUTHOR("");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("");
static char *cmd, *val;

MODULE_PARM_DESC(cmd, "Command to properly change mem_trace system");
MODULE_PARM_DESC(val, "Value(usually 0 or 1) required for certain commands");
module_param(cmd, charp, 0000);
module_param(val, charp, 0000);

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
		//init_swap_trend(32);
		//kevictd_init();
		//prefetch_buffer_init(8000);
		//activate_prefetch_buffer(1);
		//set_custom_prefetch(2);
		fetch_init(pid, proc_name, current->mm);
		evict_init();
	}
}

void mem_pattern_trace_end(int flags)
{
	set_pointer(10, kernel_noop); // clean swapin_readahead injection

	// all _fini functions check whether they have been initialized
	// before performing any free-ing so no need to do it here
	evict_fini();
	fetch_fini();
	record_fini();
}

static void mem_pattern_trace_3(int flags)
{
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

	if (memtrace_getflag(TAPE_OPS) == 0) {
		printk(KERN_INFO "mem_pattern_trace: functionality is off\n");
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

static void usage(void)
{
	printk(KERN_INFO "To enable remote I/O data path: insmod %ld"
			 "leap_functionality.ko cmd=\"remote_on\"\n",
	       sizeof(struct page));
	printk(KERN_INFO "To disable remote I/O data path: insmod "
			 "leap_functionality.ko cmd=\"remote_off\"\n");
	printk(KERN_INFO
	       "To enable leap prefetching: insmod leap_functionality.ko "
	       "cmd=\"leap\"\n");
	printk(KERN_INFO "To disable prefetching: insmod leap_functionality.ko "
			 "cmd=\"readahead\"\n");
	printk(KERN_INFO "To have swap info log: insmod leap_functionality.ko "
			 "cmd=\"log\"\n");
}

static void swap_writepage_32(struct page *page, struct writeback_control *wbc,
			      bool *skip, bool *backout)
{
	*backout = false;
	return;
}

static void no_skip_ssd_35(bool *skip_ssd)
{
	*skip_ssd = false;
}

static void print_memtrace_flags()
{
	printk(KERN_INFO "memtrace global flags:\n"
			 "Tape operations\t\t %s (tape_ops)\n"
			 "Swap SSD Optim\t\t %s (ssdopt)\n"
			 "Fastswap writes\t\t %s (async_writes)\n",
	       memtrace_getflag(TAPE_OPS) ? "ON" : "OFF",
	       memtrace_getflag(SWAP_SSD_OPTIMIZATION) ? "ON" : "OFF",
	       memtrace_getflag(FASTSWAP_ASYNCWRITES) ? "ASYNC" : "SYNC");
}

static int __init leap_functionality_init(void)
{
	// set syscall entrypoint for prefetching
	set_pointer(3, mem_pattern_trace_3);

	if (!cmd) {
		usage();
		return 0;
	}

	if (strcmp(cmd, "tape_ops") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(TAPE_OPS) :
			      memtrace_clearflag(TAPE_OPS);
	}

	if (strcmp(cmd, "ssdopt") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(SWAP_SSD_OPTIMIZATION) :
			      memtrace_clearflag(SWAP_SSD_OPTIMIZATION);
	}

	if (strcmp(cmd, "async_writes") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(FASTSWAP_ASYNCWRITES) :
			      memtrace_clearflag(FASTSWAP_ASYNCWRITES);
	}
	/*
	//		memtrace_clearflag(TAPE_OPS| SWAP_SSD_OPTIMIZATION | FASTSWAP_ASYNCWRITES);
	//		set_pointer(35, no_skip_ssd_35);
	if (strcmp(cmd, "vanilla_fastswap_ssdopt") == 0) {
		memtrace_clearflag(TAPE_OPS | FASTSWAP_ASYNCWRITES);
		memtrace_setflag(SWAP_SSD_OPTIMIZATION);
		printk(KERN_INFO "Vanilla fastswap + swap write path software "
				 "optimization\n");
	}

	if (strcmp(cmd, "fastswap_ssdopt_asyncwrites") == 0) {
		printk(KERN_INFO "Fastswap with optimized swap writing and "
				 "async writes \n");
		// backs out from sync writes which is the default in this kernel build
		set_pointer(32, swap_writepage_32);
	}

	if (strcmp(cmd, "fastwap_ssdopt_syncwrites_prefetching") == 0) {
		printk(KERN_INFO "Fastswap with optimized swap writing, sync "
				 "writes and prefetching\n");
		// sets up syscall interface injection which sets up
		// rest of necessary function links
	}

	if (strcmp(cmd, "fastwap_ssdopt_async_writes_prefetching") == 0) {
		printk(KERN_INFO "Fastswap with optimized swap writing, async "
				 "writes and prefetching\n");
		// sets up syscall interface injection which sets up
		// rest of necessary function links
		set_pointer(3, mem_pattern_trace_3);
		evict_init();
	}
*/
	print_memtrace_flags();
	return 0;
}

static void __exit leap_functionality_exit(void)
{
	int i;

	printk(KERN_DEBUG "resetting injection points to noop");
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

	// free vmallocs, in case the process crashed or used syscalls incorerclty
	record_force_clean();
	fetch_force_clean();
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
