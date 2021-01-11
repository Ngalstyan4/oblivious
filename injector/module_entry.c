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
#include "kevictd.h"


MODULE_LICENSE("");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("");
static char *cmd;
//todo: remove
static char *process_name;
MODULE_PARM_DESC(cmd, "A string, for prefetch load/unload command");
module_param(cmd, charp, 0000);
MODULE_PARM_DESC(process_name, "A string, for process name");
module_param(process_name, charp, 0000);

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
	}
}

void mem_pattern_trace_end(int flags)
{
	set_pointer(10, kernel_noop); // clean swapin_readahead injection

	// all _fini functions check whether they have been initialized
	// before performing any free-ing so no need to do it here
	fetch_fini();
	record_fini();
}

static void mem_pattern_trace_3(int flags)
{
	printk(KERN_INFO "mem_pattern_trace called from pid %d with flags: "
			 "[%s%s%s%s|%s%s%s]\n",
	       current->pid, flags & TRACE_START ? "TRACE_START" : "",
	       flags & TRACE_PAUSE ? "TRACE_PAUSE" : "",
	       flags & TRACE_RESUME ? "TRACE_RESUME" : "",
	       flags & TRACE_END ? "TRACE_END" : "",
	       flags & TRACE_RECORD ? "TRACE_RECORD" : "",
	       flags & TRACE_PREFETCH ? "TRACE_PREFETCH" : "",
	       flags & TRACE_AUTO ? "TRACE_AUTO" : ""

	       );
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

static int __init leap_functionality_init(void)
{
	// 2 is done in the switch below
	// sets up syscall interface injection which sets up
	// rest of necessary function links
	set_pointer(3, mem_pattern_trace_3);
	if (!cmd) {
		usage();
		return 0;
	}
	if (strcmp(cmd, "remote_on") == 0) {
		printk(KERN_INFO "Leap remote memory is on\n");
		//set_process_id(1);
		return 0;
	}
	if (strcmp(cmd, "remote_off") == 0) {
		printk(KERN_INFO "Leap remote memory is off\n");
		//set_process_id(0);
		return 0;
	}
	if (strcmp(cmd, "leap") == 0) {
		printk(KERN_INFO "prefetching set to LEAP\n");
		//init_swap_trend(32);
		//set_custom_prefetch(1);
		return 0;
	} else if (strcmp(cmd, "log") == 0) {
		//swap_info_log();
		return 0;
	} else if (strcmp(cmd, "readahead") == 0) {
		printk(KERN_INFO "prefetching set to LINUX READAHEAD\n");
		//set_custom_prefetch(0);
		return 0;
	} else
		usage();
	return 0;
}

static void __exit leap_functionality_exit(void)
{
	int i;

	printk(KERN_INFO "resetting injection points to noop");
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

	// free vmallocs, in case the process crashed or used syscalls incorerclty
	record_force_clean();
	fetch_force_clean();
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
