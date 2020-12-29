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

MODULE_LICENSE("");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("");
extern void kernel_noop(void);
char *cmd;
unsigned long tried = 0;
char *process_name;
struct mm_struct *mm =
	NULL; // todo:: move back to local scope once resetting ptes is figured out
pid_t process_pid = 0;
MODULE_PARM_DESC(cmd, "A string, for prefetch load/unload command");
module_param(cmd, charp, 0000);
MODULE_PARM_DESC(process_name, "A string, for process name");
module_param(process_name, charp, 0000);

void haha(void)
{
	printk(KERN_INFO "injected print statement- C Narek Galstyan");
}
EXPORT_SYMBOL(haha);
int leap_swapin_counter = 0;
void find_trend_1(void)
{
	printk(KERN_INFO "in find trend %d ", ++leap_swapin_counter);
}
EXPORT_SYMBOL(find_trend_1);

/************************** TRACING FOR MEMORY PREFETCHING BEGIN ********************************/
const unsigned long PRESENT_BIT_MASK = 1UL;
const unsigned long SPECIAL_BIT_MASK = 1UL << 58;

static unsigned long *trace = NULL;
// const char *TRACE_FILE = "/etc/trace_mmult_eigen.bin";
const char *TRACE_FILE = "/data/trace_mmult_eigen4.bin";
#define TRACE_ARRAY_SIZE 1024 * 1024 * 1024 * 2ULL
#define TRACE_LEN (TRACE_ARRAY_SIZE / sizeof(void *))
unsigned long trace_idx = 0;

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

typedef struct {
	pgd_t *pgd;
	// todo, will need p4d for newer kernels
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long address;
	bool initialized;
} vm_t;

int i = 0;
vm_t last_entry = {.initialized = false }, entry = {.initialized = false };

// Pattern-detection variables
static vm_t recent_accesses[3];
static unsigned long recent_ips[3];
static unsigned long patterns_encountered = 0;
static bool exited_alt_pattern = false;

void tracing_init()
{
	trace = vmalloc(TRACE_ARRAY_SIZE);
	if (trace == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing");
		return;
	}
	printk(KERN_DEBUG "initialized trace %p", trace);
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
static void log_pfault(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address, unsigned long pte_val)
{
	printk(KERN_DEBUG "pfault [%s | %s | %s | "
			  "%s | %s]  %lx pte: %lx [%s|%s]",
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
	bool in_alt_pattern;
	if (unlikely(trace == NULL)) {
		printk(KERN_ERR "trace not initialized");
		return;
	}

	if (process_pid == tsk->pid && trace_idx < TRACE_LEN) {

		mm = tsk->mm;
		down_read(&mm->mmap_sem);

		// walk the page table ` https://lwn.net/Articles/106177/
		//todo:: pteditor does it wrong i think,
		//it does not dereference pte when passing around
		entry.address = address;
		entry.pgd = pgd_offset(mm, address);
		entry.pud = pud_offset(entry.pgd, address);
		// todo:: to support thp, do some error checking here and see if a huge page is being allocated
		entry.pmd = pmd_offset(entry.pud, address);
		if (pmd_none(*(entry.pmd)) || pud_large(*(entry.pud))) {
			if (pmd_none(*(entry.pmd)))
				printk(KERN_ERR "pmd is noone %lx", address);
			else
				printk(KERN_ERR "pud is a large page");
			entry.pmd = NULL;
			entry.pte = NULL;
			goto error_out;
		}
		entry.pte = pte_offset_map(entry.pmd, address);
		entry.initialized = true;
		//todo:: optimze later, to return here and baybe avoid tlb flush?
		trace_maybe_set_pte(&entry, return_early);

		in_alt_pattern = check_alt_pattern(address, regs->ip);
		if (in_alt_pattern)
			printk(KERN_WARNING "IN ALT PATTERN %ld", trace_idx);
		if (entry.address != last_entry.address && //CoW?
		    !in_alt_pattern) {
			trace_clear_pte(&last_entry);
		}

		if (!in_alt_pattern && exited_alt_pattern) {
			trace_clear_pte(&recent_accesses[1]);
			exited_alt_pattern = false;
		}

	error_out:
		get_cpu();
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
		// the following, if used correctly, can trace TLB count.
		// trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
		put_cpu();

		up_read(&mm->mmap_sem);

		//todo:: change to
		//last_entry = entry;
		last_entry.pte = entry.pte;
		last_entry.address = entry.address;
		last_entry.initialized = entry.initialized;
		trace[trace_idx++] = address;

		// Push to the data structures that help us determine
		// whether we've encountered an alternating pattern
		push_to_fifos(&entry, regs->ip);

		// If we're in an alt pattern we need to know
		// the next time we're in the signal handler
		if (in_alt_pattern) {
			patterns_encountered++;
			exited_alt_pattern = true;
		}
	}
}
EXPORT_SYMBOL(do_page_fault_2);

void mem_pattern_trace_start_3(void)
{
}
EXPORT_SYMBOL(mem_pattern_trace_start_3);

void mem_pattern_trace_end_4(void)
{

	int i;
	int num_ptes_set = 0;
	printk(KERN_INFO "trace end syscall, clean up special bits");
	// it seems there is no need for this after modifying do_unmap kernel path
	// todo:: turned back on as saw some rss-counter and memory free errors that might
	// be related to this, investigate later.
	if (mm != NULL) {
		printk(KERN_DEBUG "resetting ptes first 5 addrs of trace:[%lx, "
				  "%lx, %lx, %lx, %lx]",
		       trace[0], trace[1], trace[2], trace[3], trace[4]);
		down_read(&mm->mmap_sem);
		for (i = 0; i < trace_idx; i++) {

			unsigned long address = trace[i];
			bool my_ret_early = false;
			entry.address = address;
			entry.pgd = pgd_offset(mm, address);
			if (pgd_none(*entry.pgd) || pgd_bad(*entry.pgd))
				continue;
			entry.pud = pud_offset(entry.pgd, address);
			if (pud_none(*entry.pud) || pud_bad(*entry.pud))
				continue;
			entry.pmd = pmd_offset(entry.pud, address);
			if (pmd_none(*(entry.pmd)) || pud_large(*(entry.pud))) {
				// probably because the region has been cleaned up?
				entry.pmd = NULL;
				entry.pte = NULL;
				continue;
			}
			entry.pte = pte_offset_map(entry.pmd, address);
			entry.initialized = true;
			trace_maybe_set_pte(&entry, &my_ret_early);
			if (my_ret_early)
				num_ptes_set++;
		}
		up_read(&mm->mmap_sem);

		printk(KERN_INFO
		       "done RESETTING ptes before exit, num successful reset: "
		       "%d out of %ld",
		       num_ptes_set, trace_idx);
	}
}
EXPORT_SYMBOL(mem_pattern_trace_end_4);
/************************** TRACING FOR MEMORY PREFETCHING BEND ********************************/

// debug-injecting in mm/memory.c:1138 (in function zap_pte_range)
void do_unmap_5(pte_t *pte)
{
	unsigned long pte_deref_value = (unsigned long)((*pte).pte);
	if (pte_deref_value & SPECIAL_BIT_MASK) {
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		set_pte(pte, native_make_pte(pte_deref_value));
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
// declarations copied from swap_state for injections BEGIN
struct pref_buffer {
	atomic_t head;
	atomic_t tail;
	atomic_t size;
	swp_entry_t *offset_list;
	struct page **page_data;
	spinlock_t buffer_lock;
};

extern struct pref_buffer prefetch_buffer;
// declarations copied from swap_state for injections END

int counter = 0;
int found_counter = 0;
static unsigned long *prefetch_trace = NULL;
unsigned long num_accesses = -1;
unsigned long current_pos = 0;
unsigned long PAGE_ADDR_MASK = ~0xfff;
unsigned long MAX_SEARCH_DIST = 10000;
void tape_prefetch_init()
{
	struct file *f;
	mm_segment_t old_fs = get_fs();

	prefetch_trace = vmalloc(TRACE_ARRAY_SIZE);
	if (prefetch_trace == NULL) {
		printk(KERN_ERR "unable to allocate memory for trace");
		return;
	}
	set_fs(get_ds()); // KERNEL_DS

	f = filp_open(TRACE_FILE, O_RDONLY, 0);
	if (f == NULL) {
		printk(KERN_ERR "unable to read/open file");
	} else {
		size_t count = vfs_read(f, (char *)prefetch_trace,
					TRACE_ARRAY_SIZE, &f->f_pos);
		num_accesses = count / sizeof(void *);
		printk(KERN_DEBUG "read %ld bytes which means %ld accesses",
		       count, num_accesses);
		filp_close(f, NULL);
		set_fs(old_fs);
		printk(KERN_DEBUG "read trace: %lx %lx %lx", prefetch_trace[0],
		       prefetch_trace[1], prefetch_trace[2]);
	}
}
void swapin_readahead_10(swp_entry_t *entry, gfp_t *gfp_mask,
			 struct vm_area_struct *vma, const unsigned long addr,
			 bool *goto_skip)
{
	int dist = 0;
	pte_t pte_val;
	swp_entry_t prefetch_entry; //todo naming!!?!
	vm_t vm_entry;
	struct mm_struct *mm = vma->vm_mm;
	struct page *page;

	counter++;
	if (unlikely(prefetch_trace == NULL))
		return;
	// printk(KERN_DEBUG "swaping readahead count: %d addr: %lx\n",
	//        counter++, addr);

	while (dist < MAX_SEARCH_DIST &&
	       (prefetch_trace[current_pos + dist] & PAGE_ADDR_MASK) !=
		       (addr & PAGE_ADDR_MASK))
		dist++;
	if ((prefetch_trace[current_pos + dist] & PAGE_ADDR_MASK) ==
	    (addr & PAGE_ADDR_MASK)) {
		int num_prefetch = 0;
		found_counter++;
		// walk the page table for addr START
		down_read(&mm->mmap_sem);
		for (i = 0; i < 100 && num_prefetch < 32; i++) {
			unsigned long paddr = prefetch_trace[current_pos + i];
			vm_entry.address = paddr;
			vm_entry.pgd = pgd_offset(mm, paddr);
			if (pgd_none(*vm_entry.pgd) || pgd_bad(*vm_entry.pgd))
				return;
			vm_entry.pud = pud_offset(vm_entry.pgd, paddr);
			if (pud_none(*vm_entry.pud) || pud_bad(*vm_entry.pud))
				return;
			vm_entry.pmd = pmd_offset(vm_entry.pud, paddr);
			if (pmd_none(*(vm_entry.pmd)) ||
			    pud_large(*(vm_entry.pud))) {
				// probably because the region has been cleaned up?
				vm_entry.pmd = NULL;
				vm_entry.pte = NULL;
				return;
			}
			vm_entry.pte = pte_offset_map(vm_entry.pmd, paddr);
			vm_entry.initialized = true;

			// prefetch the page if needed
			pte_val = *vm_entry.pte;
			if (pte_none(pte_val))
				continue;
			if (pte_present(pte_val))
				continue;
			prefetch_entry = pte_to_swp_entry(pte_val);
			if (unlikely(non_swap_entry(prefetch_entry)))
				continue;
			page = read_swap_cache_async(prefetch_entry, *gfp_mask,
						     vma, addr);
			if (!page)
				continue;
			num_prefetch++;
			SetPageReadahead(page);

			put_page(page); //= page_cache_release

			// printk(KERN_WARNING "page walk ahead %d %s %lx", i,
			//        !pte_none(*vm_entry.pte) &&
			// 		       pte_present(*vm_entry.pte) ?
			// 	       "PRESENT" :
			// 	       "GONE",
			//        addr & PAGE_ADDR_MASK);
		}
		printk(KERN_INFO "num prefetch %d", num_prefetch);
		lru_add_drain();
		if (num_prefetch > 10)
			*goto_skip = true;

		up_read(&mm->mmap_sem);
		// walk the page table for addr END
		current_pos += dist;
	} else {
		// printk(KERN_WARNING " lost in trace %d/%d \n", found_counter, counter);
	}
}
/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE END ++++++++++++++++++++++*/
static int get_pid_for_process(void)
{
	int pid = -1;
	struct task_struct *task;
	for_each_process (task) {
		if (strcmp(process_name, task->comm) == 0) {
			pid = task->pid;
			printk(KERN_INFO "Process id of %s process is %i\n",
			       process_name, task->pid);
		}
	}
	return pid;
}

static int process_find_init(void)
{
	int pid = -1;
	printk(KERN_INFO "Initiating process find for %s!\n", process_name);
	if (!process_name) {
		printk(KERN_INFO "Invalid process_name\n");
		return -1;
	}
	while (pid == -1) {
		pid = get_pid_for_process();
		tried++;
		if (tried > 30)
			break;
		if (pid == -1)
			msleep(1000); // milisecond sleep
	}
	if (pid != -1) {
		process_pid = pid;
		tracing_init();
		set_process_id(pid);
		set_pointer(2, do_page_fault_2);
		printk("PROCESS ID set for remote I/O -> %ld\n",
		       get_process_id());
		printk(KERN_INFO "started tracing");
	} else {
		printk(KERN_INFO "Failed to track process within %ld seconds\n",
		       tried);
	}
	return 0;
}

static void usage(void)
{
	printk(KERN_INFO "To initialize tracing for a process by name insmod "
			 "leap_functionality.ko process_name=\"mmult_eigen\" "
			 "cmd=\"trace_init\"\n");
	printk(KERN_INFO "To enable remote I/O data path: insmod "
			 "leap_functionality.ko cmd=\"remote_on\"\n");
	printk(KERN_INFO "To disable remote I/O data path: insmod "
			 "leap_functionality.ko cmd=\"remote_off\"\n");
	printk(KERN_INFO
	       "To enable tape prefetching: insmod leap_functionality.ko "
	       "cmd=\"tape\"\n");
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
	set_pointer(3, mem_pattern_trace_start_3);
	set_pointer(4, mem_pattern_trace_end_4);
	//set_pointer(5, do_unmap_5);
	set_pointer(10, swapin_readahead_10);
	if (!cmd) {
		usage();
		return 0;
	}
	if (strcmp(cmd, "trace_init") == 0) {
		process_find_init();
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
	if (strcmp(cmd, "tape") == 0) {
		printk(KERN_INFO "prefetching set to TAPE");

		// !IMPORTANT TODO:: see why the line below is necessary and
		// what from leap swap system we are depending on
		// faults on ? swap_state:166 (log_swap_trend)
		init_swap_trend(32);
		tape_prefetch_init();
		set_custom_prefetch(2);
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
	printk(KERN_INFO "found %d/%d", found_counter, counter);
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

	if (prefetch_trace != NULL) {
		vfree(prefetch_trace);
	}

	if (trace != NULL) {
		// Write trace to file
		// docs ` https://www.howtoforge.com/reading-files-from-the-linux-kernel-space-module-driver-fedora-14
		// https://www.linuxjournal.com/article/8110
		struct file *f;
		mm_segment_t old_fs = get_fs();
		set_fs(get_ds()); // KERNEL_DS

		f = filp_open(TRACE_FILE, O_CREAT | O_WRONLY, 0644);
		if (f == NULL) {
			printk(KERN_ERR "unable to create/open file");
		} else {

			long left_to_write = trace_idx * sizeof(void *);
			char *buf = (char *)trace;
			printk(KERN_INFO "num accesses:  %ld", trace_idx);
			while (left_to_write > 0) {
				// todo:: cannot write to larger than 2g from kernel
				// fixed in newer kernels, I guess just upgrade?
				size_t count = vfs_write(f, buf, left_to_write,
							 &f->f_pos);
				if (count < 0)
					break;
				printk(KERN_INFO
				       "size_t can not be smaller than zero");
				if (((long)(count)) < 0)
					break;
				printk(KERN_DEBUG "write %ld bytes out of %ld "
						  "left to writ and %ld total",
				       count, left_to_write,
				       trace_idx * sizeof(void *));
				left_to_write -= count;
				buf += count;
				printk("Wrote buffer size %ld bytes", count);
			}
			printk(KERN_DEBUG "wrote to file, left to write %ld",
			       left_to_write);
			filp_close(f, NULL);
			set_fs(old_fs);
		}
		vfree(trace);
	}
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
