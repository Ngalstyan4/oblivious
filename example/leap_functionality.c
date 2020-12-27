#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/socket.h>

#include <linux/fs.h> // Needed by filp
#include <asm/uaccess.h> // Needed by segment descriptors
#include <asm/tlbflush.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hasan Al Maruf");
MODULE_DESCRIPTION("Kernel module to enable/disable Leap components");
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

void find_trend_1(void)
{
	printk(KERN_INFO "in find trend");
}
EXPORT_SYMBOL(find_trend_1);
/************************** TRACING LOG BEGIN ********************************/
const unsigned long PRESENT_BIT_MASK = 1UL;
const unsigned long SPECIAL_BIT_MASK = 1UL << 58;

static unsigned long *trace = NULL;
#define TRACE_ARRAY_SIZE 1024 * 1024 * 1024 * 2ULL
#define TRACE_LEN (TRACE_ARRAY_SIZE / sizeof(void *))
unsigned long trace_idx = 0;
void tracing_init()
{
	trace = vmalloc(TRACE_ARRAY_SIZE);
	if (trace == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing");
		return;
	}
	printk(KERN_DEBUG "initialized trace %p", trace);
}
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
unsigned long track_addr = -1;
__always_inline void trace_maybe_set_pte(vm_t *entry, bool *return_early)
{
	unsigned long pte_deref_value = (unsigned long)((*entry->pte).pte);
	*return_early = false;

	if (unlikely(entry->initialized == false))
		return;

	if (pte_deref_value & SPECIAL_BIT_MASK) {
		printk(KERN_DEBUG "clearing special bit for pte %lx",
		       pte_deref_value);
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		set_pte(entry->pte, native_make_pte(pte_deref_value));
		printk(KERN_DEBUG "cleared special bit for pte %lx",
		       pte_deref_value);
		*return_early = true;
	}
}

// used to unmap the last entry
static void trace_clear_pte(vm_t *entry)
{
	// if previous fault was a pmd allocation fault, we will not have pte
	if (!entry->pte)
		return;
	unsigned long pte_deref_value = (unsigned long)((*entry->pte).pte);

	if (unlikely(entry->initialized == false))
		return;
	// normally, there would just be allocation faults. but for tracing we want to see *all* page
	// accesses so we make sure that form kernel's point of view the page that the application
	// accesssed just before faulting on this page, is not present in mememory. Additionally,
	// we set the special bit (bit 58, see x86 manual) to later know that we are responsible
	// for the fault.
	//todo:: do not reuse var and repeat code.
	printk(KERN_DEBUG "clearing pte for last entry pte: %lx",
	       pte_deref_value);
	pte_deref_value &= ~PRESENT_BIT_MASK;
	pte_deref_value |= SPECIAL_BIT_MASK;
	printk(KERN_DEBUG "clear_ed pte for last entry pte: %lx",
	       pte_deref_value);
	set_pte(entry->pte, native_make_pte(pte_deref_value));
}

void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
		     unsigned long address, struct task_struct *tsk,
		     bool *return_early, int magic)
{
	if (unlikely(trace == NULL)) {
		printk(KERN_ERR "trace not initialized");
		return;
	}

	if (likely(process_pid == tsk->pid && trace_idx < TRACE_LEN)) {

		mm = tsk->mm;
		down_read(&mm->mmap_sem);

		// walk the page table ` https://lwn.net/Articles/106177/
		//todo:: pteditor does it wron i think,
		//it does not dereference pte when passing around
		entry.address = address;
		entry.pgd = pgd_offset(mm, address);
		entry.pud = pud_offset(entry.pgd, address);
		entry.pmd = pmd_offset(entry.pud, address);
		if (pmd_none(*(entry.pmd)) || pud_large(*(entry.pud))) {
			track_addr = address;
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
		if (entry.address != last_entry.address //CoW?
		    ) {
			trace_clear_pte(&last_entry);
		}
	error_out:
		up_read(&mm->mmap_sem);
		//todo:: optimze later, to return here and baybe avoid tlb flush?

		last_entry = entry;
		trace[trace_idx++] = address;

		get_cpu();
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
		// the following, if used correctly, can trace TLB count.
		// trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
		put_cpu();

		// printk(KERN_DEBUG "made it to the end!");
	}

	if (process_pid == tsk->pid &&
	    (track_addr == address || i++ < 3 || (error_code & PF_INSTR))) {
		unsigned long pte_deref_value = 0;
		// (unsigned long)((*entry.pte).pte);
		printk(KERN_INFO "pfault [%s | %s | %s | "
				 "%s | %s]  %lx %d pte: %lx [%s|%s]",
		       error_code & PF_PROT ? "PROT" : "",
		       error_code & PF_WRITE ? "WRITE" : "READ",
		       error_code & PF_USER ? "USER" : "KERNEL",
		       error_code & PF_RSVD ? "SPEC" : "",
		       error_code & PF_INSTR ? "INSTR" : "", address, tsk->pid,
		       pte_deref_value,
		       pte_deref_value & PRESENT_BIT_MASK ? "PRESENT" : "",
		       pte_deref_value & SPECIAL_BIT_MASK ? "SPEC" : "");
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
	if (mm != NULL) {
		printk(KERN_INFO "resetting ptes [%lx, %lx, %lx, %lx, %lx]",
		       trace[0], trace[1], trace[2], trace[3], trace[4]);
		down_read(&mm->mmap_sem);
		for (i = 0; i < trace_idx; i++) {

			unsigned long address = trace[i];
			bool my_ret_early = false;
			printk(KERN_INFO "addr %lx", address);
			entry.address = address;
			entry.pgd = pgd_offset(mm, address);
			if (pgd_none(*entry.pgd) || pgd_bad(*entry.pgd))
				continue;
			entry.pud = pud_offset(entry.pgd, address);
			if (pud_none(*entry.pud) || pud_bad(*entry.pud))
				continue;
			entry.pmd = pmd_offset(entry.pud, address);
			if (pmd_none(*(entry.pmd)) || pud_large(*(entry.pud))) {
				track_addr = address;
				if (pmd_none(*(entry.pmd)))
					printk(KERN_ERR "pmd is noone %lx",
					       address);
				else
					printk(KERN_ERR "pud is a large page");
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

		printk(KERN_INFO "done RESETTING ptes before exit, num reset: "
				 "%d out of %ld",
		       num_ptes_set, trace_idx);
		msleep(1000);
	}
}
EXPORT_SYMBOL(mem_pattern_trace_end_4);
/************************** TRACING LOG END ********************************/

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
	printk(KERN_INFO "To enable remote I/O data path: insmod "
			 "leap_functionality.ko process_name=\"tunkrank\" "
			 "cmd=\"init\"\n");
	printk(KERN_INFO "To disable remote I/O data path: insmod "
			 "leap_functionality.ko cmd=\"fini\"\n");
	printk(KERN_INFO "To enable prefetching: insmod leap_functionality.ko "
			 "cmd=\"prefetch\"\n");
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
	// set_pointer(2, do_page_fault_2); // <-- set up in  proc attach init
	if (!cmd) {
		usage();
		return 0;
	}
	if (strcmp(cmd, "init") == 0) {
		process_find_init();
		return 0;
	}
	if (strcmp(cmd, "fini") == 0) {
		set_process_id(0);
		return 0;
	}
	if (strcmp(cmd, "prefetch") == 0) {
		init_swap_trend(32);
		set_custom_prefetch(1);
		return 0;
	} else if (strcmp(cmd, "log") == 0) {
		swap_info_log();
		return 0;
	} else if (strcmp(cmd, "readahead") == 0) {
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
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);
	printk(KERN_INFO "done reseting injection points");

	if (trace != NULL) {
		// Write trace to file
		// docs ` https://www.howtoforge.com/reading-files-from-the-linux-kernel-space-module-driver-fedora-14
		// https://www.linuxjournal.com/article/8110
		struct file *f;
		mm_segment_t old_fs = get_fs();
		set_fs(get_ds()); // KERNEL_DS

		f = filp_open("/etc/trace_mmult_eigen.bin", O_CREAT | O_WRONLY,
			      0644);
		if (f == NULL) {
			printk(KERN_ERR "unable to create/open file");
		} else {
			vfs_write(f, (char *)trace, trace_idx * sizeof(void *),
				  &f->f_pos);
			printk(KERN_DEBUG "Wrote to file");
			filp_close(f, NULL);
			set_fs(old_fs);
		}
		vfree(trace);
		printk(KERN_INFO "done writing trace to file");
	}
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
