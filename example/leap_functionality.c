#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/socket.h>

#include <linux/fs.h> // Needed by filp
#include <asm/uaccess.h> // Needed by segment descriptors
#include <asm/tlbflush.h>

#include "utils.h"
#include "tracing.h"
#include "kevictd.h"

#include "page_buffer.h"

// controlls whether data structures are maintained for in alt pattern
// or it is assumed that this never happens
#define IN_ALT_PATTERN_CHECKS 1

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

extern void kernel_noop(void);

/*
   * Page fault error code bits:
 *
 *   bit 0 ==	 0: no page found	1: protection fault
 *   bit 1 ==	 0: read access		1: write access
 *   bit 2 ==	 0: kernel-mode access	1: user-mode access
 *   bit 3 ==				1: use of reserved bit detected
 *   bit 4 ==				1: fault was an instruction fetch
 */
enum x86_pf_error_code {

	PF_PROT = 1 << 0,
	PF_WRITE = 1 << 1,
	PF_USER = 1 << 2,
	PF_RSVD = 1 << 3,
	PF_INSTR = 1 << 4,
};

const unsigned long PAGE_ADDR_MASK = ~0xfff;
const unsigned long MAX_SEARCH_DIST = 2000;
const unsigned long PRESENT_BIT_MASK = 1UL;
const unsigned long SPECIAL_BIT_MASK = 1UL << 58;

typedef struct {
	pgd_t *pgd;
	// todo, will need p4d for newer kernels
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long address;
	bool initialized;
} vm_t;

#define TRACE_FILEPATH_LEN 256
#define TRACE_ARRAY_SIZE 1024 * 1024 * 1024 * 5ULL
#define TRACE_LEN (TRACE_ARRAY_SIZE / sizeof(void *))
static char trace_filepath[TRACE_FILEPATH_LEN];
typedef struct {
	pid_t process_pid;
	unsigned long *accesses;
	unsigned long pos;

	vm_t last_entry;
	vm_t entry;
	unsigned long alt_pattern_counter;

} tracing_state;

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

static tracing_state trace;
//todo:: switch back to static
prefetching_state fetch;

// Pattern-detection variables todo:: elliminate these!
static vm_t recent_accesses[3];
static unsigned long recent_ips[3];
static unsigned long patterns_encountered = 0;
static bool exited_alt_pattern = false;
static bool in_alt_pattern = false;

void fetch_init();
void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
		     unsigned long address, struct task_struct *tsk,
		     bool *return_early, int magic);
void do_page_fault_fetch_2(struct pt_regs *regs, unsigned long error_code,
			   unsigned long address, struct task_struct *tsk,
			   bool *return_early, int magic);
void haha(void)
{
	printk(KERN_INFO "injected print statement- C Narek Galstyan");
}
EXPORT_SYMBOL(haha);
void find_trend_1(void)
{
	static int leap_swapin_counter = 0;
	printk(KERN_INFO "in find trend %d ", ++leap_swapin_counter);
}
EXPORT_SYMBOL(find_trend_1);

/************************** TRACING FOR MEMORY PREFETCHING BEGIN ********************************/
void trace_init(pid_t pid)
{
	memset(&trace, 0, sizeof(trace));
	trace.process_pid = pid;
	//todo:: put everything on tracing
	trace.accesses = vmalloc(TRACE_ARRAY_SIZE);
	if (trace.accesses == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		return;
	}

	set_pointer(2, do_page_fault_2);
}

__always_inline void trace_maybe_set_pte(vm_t *entry, bool *return_early)
{
	unsigned long pte_deref_value = (unsigned long)((*entry->pte).pte);
	*return_early = false;

	if (unlikely(entry->initialized == false))
		return;

	if (pte_deref_value & SPECIAL_BIT_MASK) {
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		set_pte(entry->pte, native_make_pte(pte_deref_value));
		*return_early = true;
	}
}

// used to unmap the last entry
static void trace_clear_pte(vm_t *entry)
{
	unsigned long pte_deref_value;
	// if previous fault was a pmd allocation fault, we will not have pte
	if (!entry->pte)
		return;
	pte_deref_value = (unsigned long)((*entry->pte).pte);

	if (unlikely(entry->initialized == false))
		return;
	// normally, there would just be allocation faults. but for tracing we want to see *all* page
	// accesses so we make sure that form kernel's point of view the page that the application
	// accesssed just before faulting on this page, is not present in mememory. Additionally,
	// we set the special bit (bit 58, see x86 manual) to later know that we are responsible
	// for the fault.
	pte_deref_value &= ~PRESENT_BIT_MASK;
	pte_deref_value |= SPECIAL_BIT_MASK;
	set_pte(entry->pte, native_make_pte(pte_deref_value));
}

#ifdef IN_ALT_PATTERN_CHECKS
/************************** ALT PATTERN CHECK BEGIN ********************************/

// Returns true if we're stuck in the ABAB pattern that causes programs to hang
static bool check_alt_pattern(unsigned long faulting_addr,
			      unsigned long faulting_ip)
{
	// Check for the alternating pattern in faulting addresses
	bool alt_pattern_addr =
		recent_accesses[0].address == recent_accesses[2].address &&
		recent_accesses[1].address == faulting_addr;

	// The last 4 instructions were identical
	bool all_same_inst = recent_ips[0] == recent_ips[1] &&
			     recent_ips[1] == recent_ips[2] &&
			     recent_ips[2] == faulting_ip;

	// Both conditions need to be true for us to be in the pattern
	return alt_pattern_addr && all_same_inst;
}

static void push_to_fifos(vm_t *entry, unsigned long faulting_ip)
{
	recent_accesses[0] = recent_accesses[1];
	recent_accesses[1] = recent_accesses[2];
	recent_accesses[2] = *entry;
	recent_ips[0] = recent_ips[1];
	recent_ips[1] = recent_ips[2];
	recent_ips[2] = faulting_ip;

	// printk(KERN_DEBUG "last 4 from last: %lx %lx %lx ", r[2].address,
	//        r[1].address, r[0].address);
}
/************************** ALT PATTERN CHECK END  ********************************/
#endif // IN_ALT_PATTERN_CHECKS

static void log_pfault(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address, unsigned long pte_val)
{
	printk(KERN_DEBUG "pfault [%s | %s | %s | "
			  "%s | %s]  %lx pte: %lx [%s|%s]\n",
	       error_code & PF_PROT ? "PROT" : "",
	       error_code & PF_WRITE ? "WRITE" : "READ",
	       error_code & PF_USER ? "USER" : "KERNEL",
	       error_code & PF_RSVD ? "SPEC" : "",
	       error_code & PF_INSTR ? "INSTR" : "", address, pte_val,
	       pte_val & PRESENT_BIT_MASK ? "PRESENT" : "",
	       pte_val & SPECIAL_BIT_MASK ? "SPEC" : "");
}

void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
		     unsigned long address, struct task_struct *tsk,
		     bool *return_early, int magic)
{
	if (unlikely(trace.accesses == NULL)) {
		printk(KERN_ERR "trace not initialized\n");
		return;
	}

	if (trace.process_pid == tsk->pid && trace.pos < TRACE_LEN) {

		struct mm_struct *mm = tsk->mm;
		down_read(&mm->mmap_sem);

		// walk the page table ` https://lwn.net/Articles/106177/
		//todo:: pteditor does it wrong i think,
		//it does not dereference pte when passing around
		trace.entry.address = address;
		trace.entry.pgd = pgd_offset(mm, address);
		trace.entry.pud = pud_offset(trace.entry.pgd, address);
		// todo:: to support thp, do some error checking here and see if a huge page is being allocated
		trace.entry.pmd = pmd_offset(trace.entry.pud, address);
		if (pmd_none(*(trace.entry.pmd)) ||
		    pud_large(*(trace.entry.pud))) {
			if (pmd_none(*(trace.entry.pmd)))
				printk(KERN_WARNING "pmd is noone %lx",
				       address);
			else
				printk(KERN_ERR "pud is a large page");
			trace.entry.pmd = NULL;
			trace.entry.pte = NULL;
			goto error_out;
		}
		//todo:: pte_offset_map_lock<-- what is this? when whould I need to take a lock?
		trace.entry.pte = pte_offset_map(trace.entry.pmd, address);
		trace.entry.initialized = true;
		//todo:: optimze later, to return here and maybe avoid tlb flush?
		trace_maybe_set_pte(&trace.entry, return_early);
#ifdef IN_ALT_PATTERN_CHECKS
		in_alt_pattern = check_alt_pattern(address, regs->ip);
		// todo:: investigate:: sometimes running the same tracing
		// second time gets rid of all alt pattern issues
		// maybe happening only after a fresh reboot. probably
		// osmething to do with faulting on INSTR addresses
		if (in_alt_pattern)
			trace.alt_pattern_counter++;

		if (!in_alt_pattern && exited_alt_pattern) {
			trace_clear_pte(&recent_accesses[1]);
			exited_alt_pattern = false;
		}
#endif // IN_ALT_PATTERN_CHECKS
		if (likely(trace.entry.address !=
				   trace.last_entry.address && //CoW?
			   !in_alt_pattern)) {
			trace_clear_pte(&trace.last_entry);
		}

	error_out:
		get_cpu();
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
		// the following, if used correctly, can trace TLB count.
		// trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
		put_cpu();

		up_read(&mm->mmap_sem);

		trace.last_entry = trace.entry;
		trace.accesses[trace.pos++] = address & PAGE_ADDR_MASK;
#ifdef IN_ALT_PATTERN_CHECKS
		// Push to the data structures that help us determine
		// whether we've encountered an alternating pattern
		push_to_fifos(&trace.entry, regs->ip);

		// If we're in an alt pattern we need to know
		// the next time we're in the signal handler
		if (in_alt_pattern) {
			patterns_encountered++;
			exited_alt_pattern = true;
		}
#endif // IN_ALT_PATTERN_CHECKS
	}
}
EXPORT_SYMBOL(do_page_fault_2);

void mem_pattern_trace_start(int flags)
{
	struct kstat trace_stat;
	int trace_missing;
	pid_t pid = current->pid;
	const char *proc_name = current->comm;
	mm_segment_t old_fs = get_fs();
	//todo:: current task_sruct has min_flt, maj_flt members, maybe use?
	snprintf(trace_filepath, TRACE_FILEPATH_LEN, "/data/traces/%s.bin",
		 proc_name);
	// in case path is too long, truncate;
	trace_filepath[TRACE_FILEPATH_LEN - 1] = '\0';
	set_fs(get_ds()); // KERNEL_DS
	trace_missing = 0 != vfs_stat(trace_filepath, &trace_stat);
	set_fs(old_fs);

	if (flags & TRACE_AUTO) {
		if (trace_missing)
			flags |= TRACE_RECORD;
		else
			flags |= TRACE_PREFETCH;
	}

	printk(KERN_INFO "%s%s%s for PROCESS with pid %d\n",
	       flags & TRACE_AUTO ? "AUTO-" : "",
	       flags & TRACE_RECORD ? "RECORDING" : "",
	       flags & TRACE_PREFETCH ? "PREFETCHING" : "", pid);

	if (flags & TRACE_RECORD) {
		trace_init(pid);

	} else if (flags & TRACE_PREFETCH) {
		init_swap_trend(32);
		kevictd_init();
		//prefetch_buffer_init(8000);
		//activate_prefetch_buffer(1);
		set_custom_prefetch(2);
		fetch_init(pid, current->mm);
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

	if (trace.accesses != NULL) {
		// Write trace to file
		// docs ` https://www.howtoforge.com/reading-files-from-the-linux-kernel-space-module-driver-fedora-14
		// https://www.linuxjournal.com/article/8110
		struct file *f;

		mm_segment_t old_fs = get_fs();
		set_fs(get_ds()); // KERNEL_DS

		f = filp_open(trace_filepath, O_CREAT | O_WRONLY | O_LARGEFILE,
			      0644);
		if (IS_ERR(f)) {
			printk(KERN_ERR "unable to create/open file ERR: %ld\n",
			       PTR_ERR(f));
		} else {
			long left_to_write = trace.pos * sizeof(void *);
			char *buf = (char *)trace.accesses;
			printk(KERN_DEBUG
			       "Writing recorded trace (num accesses=%ld)",
			       trace.pos);
			while (left_to_write > 0) {
				// todo:: cannot write to larger than 2g from kernel
				// fixed in newer kernels, I guess just upgrade?
				size_t count = vfs_write(f, buf, left_to_write,
							 &f->f_pos);

				//size_t can not be smaller than zero
				if (((long)(count)) < 0) {
					printk(KERN_ERR "Failed writing. "
							"errno=%ld, left to "
							"write %ld\n",
					       count, left_to_write);
					break;
				}
				printk(KERN_DEBUG
				       "wrote %ld bytes out of %ld "
				       "left to write and %ld total\n",
				       count, left_to_write,
				       trace.pos * sizeof(void *));
				left_to_write -= count;
				buf += count;
			}

			filp_close(f, NULL);
			set_fs(old_fs);
		}
		if (trace.alt_pattern_counter)
			printk(KERN_ERR
			       "ALT PATTERN encountered %ld/%ld times\n",
			       trace.alt_pattern_counter, trace.pos);

		vfree(trace.accesses);
		// todo:: should not need once moved to kernel
		trace.accesses = NULL;
	}
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

/************************** TRACING FOR MEMORY PREFETCHING BEND ********************************/

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
			for (i = 0; i < 100 && num_prefetch < 64; i++) {
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

void fetch_init(pid_t pid, struct mm_struct *mm)
{
	struct file *f;
	mm_segment_t old_fs = get_fs();
	unsigned long *buf = vmalloc(TRACE_ARRAY_SIZE);
	size_t count;
	if (buf == NULL) {
		printk(KERN_ERR
		       "unable to allocate memory for reading the trace\n");
		return;
	}

	memset(&fetch, 0, sizeof(fetch));

	set_fs(get_ds()); // KERNEL_DS
	f = filp_open(trace_filepath, O_RDONLY | O_LARGEFILE, 0);
	if (f == NULL) {
		printk(KERN_ERR "unable to read/open file\n");
		vfree(buf);
		return;
	} else {
		count = vfs_read(f, (char *)buf, TRACE_ARRAY_SIZE, &f->f_pos);
		fetch.num_accesses = count / sizeof(void *);
		printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
		       count, fetch.num_accesses);
		filp_close(f, NULL);
		set_fs(old_fs);
	}

	fetch.accesses = buf;
	fetch.process_pid = pid;
	fetch.mm = mm;
	fetch.prefetch_start = true; // can be used to pause and resume
	set_pointer(2, do_page_fault_fetch_2);
}

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
	set_pointer(0, haha);
	set_pointer(1, find_trend_1);
	// 2 is done in the switch below
	// sets up syscall interface injection which sets up
	// rest of necessary function links
	set_pointer(3, mem_pattern_trace_3);
	//set_pointer(5, do_unmap_5);
	if (!cmd) {
		usage();
		return 0;
	}
	if (strcmp(cmd, "remote_on") == 0) {
		printk(KERN_INFO "Leap remote memory is on\n");
		set_process_id(1);
		return 0;
	}
	if (strcmp(cmd, "remote_off") == 0) {
		printk(KERN_INFO "Leap remote memory is off\n");
		set_process_id(0);
		return 0;
	}
	if (strcmp(cmd, "leap") == 0) {
		printk(KERN_INFO "prefetching set to LEAP\n");
		init_swap_trend(32);
		set_custom_prefetch(1);
		return 0;
	} else if (strcmp(cmd, "log") == 0) {
		swap_info_log();
		return 0;
	} else if (strcmp(cmd, "readahead") == 0) {
		printk(KERN_INFO "prefetching set to LINUX READAHEAD\n");
		set_custom_prefetch(0);
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

	// free vmallocs, in case the process crashed or used syscalls incorerclty
	if (trace.accesses)
		vfree(trace.accesses);
	if (fetch.accesses)
		vfree(fetch.accesses);
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
