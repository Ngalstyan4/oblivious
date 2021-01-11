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

const unsigned long MAX_SEARCH_DIST = 2000;

typedef struct {
	pid_t process_pid;
	int counter;
	int found_counter;
	int num_fault;
	struct mm_struct *mm;
	unsigned long *accesses;
	unsigned long num_accesses;
	unsigned long pos;
	unsigned long next_fetch;
	// controlls whether swapin_readahead will use tape to prefetch or not
	bool prefetch_start;
} prefetching_state;

//todo:: switch back to static
prefetching_state fetch;

void fetch_init();
void do_page_fault_fetch_2(struct pt_regs *regs, unsigned long error_code,
			   unsigned long address, struct task_struct *tsk,
			   bool *return_early, int magic);

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
		kevictd_init();
		//prefetch_buffer_init(8000);
		//activate_prefetch_buffer(1);
		//set_custom_prefetch(2);
		//fetch_init(pid, current->mm);
	}
}

void mem_pattern_trace_end(int flags)
{
	set_pointer(2, kernel_noop); // clean do page fault injection
	set_pointer(10, kernel_noop); // clean swapin_readahead injection

	// the easy case, if we were prefetching, just free the tape we read into memory
	if (fetch.accesses != NULL) {
		kevictd_fini();
		fetch.mm = NULL;
		printk(KERN_INFO "found %d/%d page faults: min:%ld, maj: %ld\n",
		       fetch.found_counter, fetch.counter, current->min_flt,
		       current->maj_flt);
		vfree(fetch.accesses);
		fetch.accesses = NULL; // todo:: should not need once in kernel
	}

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
		kevictd_init();
		return;
	}
	if (flags & KEVICTD_FINI) {
		kevictd_fini();
		return;
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
void do_page_fault_fetch_2(struct pt_regs *regs, unsigned long error_code,
			   unsigned long address, struct task_struct *tsk,
			   bool *return_early, int magic)
{
	if (unlikely(fetch.accesses == NULL)) {
		printk(KERN_ERR "fetch trace not initialized\n");
		return;
	}

	if (fetch.process_pid == tsk->pid) {

		int i;
		int dist = 0;
		int num_prefetch = 0;
		fetch.num_fault++;
		while (dist < MAX_SEARCH_DIST &&
		       (fetch.accesses[fetch.pos + dist] & PAGE_ADDR_MASK) !=
			       (address & PAGE_ADDR_MASK))
			dist++;
		if ((fetch.accesses[fetch.pos + dist] & PAGE_ADDR_MASK) ==
		    (address & PAGE_ADDR_MASK)) {
			fetch.pos += dist;
			fetch.counter++;
			// printk("fault num:%d %lx fetch pos:%ld\n",
			//        fetch.num_fault, address & PAGE_ADDR_MASK,
			//        fetch.pos);
		}

		if (fetch.pos >= fetch.next_fetch) {
			down_read(&fetch.mm->mmap_sem);
			//debug_print_prefetch();
			for (i = 0; i < 100 && num_prefetch < 32; i++) {
				unsigned long paddr =
					fetch.accesses[fetch.pos + i];

				if (prefetch_addr(paddr, fetch.mm) == true)
					num_prefetch++;
			}
			//printk(KERN_INFO "num prefetch-%d, ii %d", num_prefetch, i);
			fetch.found_counter++;
			fetch.next_fetch = fetch.pos + i;
			if (i >= 5)
				fetch.next_fetch -= 5;
			// printk(KERN_INFO "num prefetch %d\n", num_prefetch);
			lru_add_drain();
			up_read(&fetch.mm->mmap_sem);
		}
	}
}

//void fetch_init(pid_t pid, struct mm_struct *mm)
//{
//	struct file *f;
//	mm_segment_t old_fs = get_fs();
//	unsigned long *buf = vmalloc(TRACE_ARRAY_SIZE);
//	size_t count;
//	if (buf == NULL) {
//		printk(KERN_ERR
//		       "unable to allocate memory for reading the trace\n");
//		return;
//	}
//
//	memset(&fetch, 0, sizeof(fetch));
//
//	set_fs(get_ds()); // KERNEL_DS
//	f = filp_open(trace_filepath, O_RDONLY | O_LARGEFILE, 0);
//	if (f == NULL) {
//		printk(KERN_ERR "unable to read/open file\n");
//		vfree(buf);
//		return;
//	} else {
//		count = vfs_read(f, (char *)buf, TRACE_ARRAY_SIZE, &f->f_pos);
//		fetch.num_accesses = count / sizeof(void *);
//		printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
//		       count, fetch.num_accesses);
//		filp_close(f, NULL);
//		set_fs(old_fs);
//	}
//
//	fetch.accesses = buf;
//	fetch.process_pid = pid;
//	fetch.mm = mm;
//	fetch.prefetch_start = true; // can be used to pause and resume
//	set_pointer(2, do_page_fault_fetch_2);
//}

//  void swapin_readahead_10(swp_entry_t *entry, gfp_t *gfp_mask,
//  			 struct vm_area_struct *vma, const unsigned long addr,
//  			 bool *goto_skip)
//  {
//  }
/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE END ++++++++++++++++++++++*/

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
	printk(KERN_INFO "found %d/%d\n", fetch.found_counter, fetch.counter);
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

	record_force_clean();
	// free vmallocs, in case the process crashed or used syscalls incorerclty
	if (fetch.accesses)
		vfree(fetch.accesses);
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
