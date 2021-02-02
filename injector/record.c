#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>

#include <linux/injections.h>

#include "common.h"

#define TRACE_ARRAY_SIZE 1024 * 1024 * 1024 * 20ULL
#define TRACE_MAX_LEN (TRACE_ARRAY_SIZE / sizeof(void *))
// controlls whether data structures are maintained for in alt pattern
// or it is assumed that this never happens
#define IN_ALT_PATTERN_CHECKS 0

typedef struct {
	pgd_t *pgd;
	// todo, will need p4d for newer kernels
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long address;
	bool initialized;
} vm_t;

typedef struct {
	pid_t process_pid;
	unsigned long *accesses;
	unsigned long pos;
	char filepath[FILEPATH_LEN];

	vm_t last_entry;
	vm_t entry;
	unsigned long alt_pattern_counter;

	unsigned long microset_size;
	unsigned long microset_pos;
	vm_t *microset;
} tracing_state;
static tracing_state trace;

// Pattern-detection variables todo:: elliminate these!
static vm_t recent_accesses[3];
static unsigned long recent_ips[3];
static unsigned long patterns_encountered = 0;
static bool exited_alt_pattern = false;
static bool in_alt_pattern = false;

static void do_unmap_5(pte_t *pte);
static void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address, struct task_struct *tsk,
			    bool *return_early, int magic);

void record_init(pid_t pid, const char *proc_name)
{
	char trace_filepath[FILEPATH_LEN];

	snprintf(trace_filepath, FILEPATH_LEN, TRACE_FILE_FMT, proc_name);

	// in case path is too long, truncate;
	trace_filepath[FILEPATH_LEN - 1] = '\0';

	memset(&trace, 0, sizeof(trace));
	memcpy(trace.filepath, trace_filepath, FILEPATH_LEN);
	trace.process_pid = pid;

	trace.accesses = vmalloc(TRACE_ARRAY_SIZE);
	if (trace.accesses == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		return;
	}

	trace.microset_size = 3;
	trace.microset_pos = 0;
	trace.microset = vmalloc(trace.microset_size * sizeof(vm_t));
	if (trace.microset == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		vfree(trace.accesses);
		trace.accesses = NULL;
		return;
	}
	memset(trace.microset, 0x00, trace.microset_size * sizeof(vm_t));

	set_pointer(5, do_unmap_5);
	set_pointer(6, do_unmap_5); //<-- for handle_pte_fault
	set_pointer(2, do_page_fault_2);
}

bool record_initialized()
{
	return trace.accesses != NULL;
}

void record_fini()
{
	set_pointer(2, kernel_noop);
	// CANNOT reset do_unmap (pointer at index 5, do_unmap5) since it wil
	// be necessary upon process exit and this syscall is called
	// by the process therefore it is still alive

	if (record_initialized()) {
		if (trace.pos >= TRACE_MAX_LEN) {
			printk(KERN_ERR "Ran out of buffer space");
			printk(KERN_ERR "Proc mem pattern not fully recorded\n"
					"please increase buffer "
					"size(TRACE_ARRAY_SIZE) and rerun\n");
		}
		write_trace(trace.filepath, (const char *)trace.accesses,
			    trace.pos * sizeof(void *));
		if (trace.alt_pattern_counter)
			printk(KERN_ERR
			       "ALT PATTERN encountered %ld/%ld times\n",
			       trace.alt_pattern_counter, trace.pos);

		vfree(trace.microset);

		vfree(trace.accesses);
		// make sure next call to record_initialized() will return false;
		trace.accesses = NULL;
	}
}

// in case we faulted while tracing and therefore trace_fini() was never called, record_force_clean can be called
// upon module cleanup
void record_force_clean()
{
	if (trace.accesses) {
		vfree(trace.microset);
		vfree(trace.accesses);
		trace.accesses = NULL;
	}
}

static bool vm_init(vm_t* entry, struct mm_struct* mm, unsigned long address) {
	// walk the page table ` https://lwn.net/Articles/106177/
	//todo:: pteditor does it wrong i think,
	//it does not dereference pte when passing around
	entry->address = address;
	entry->pgd = pgd_offset(mm, address);
	entry->pud = pud_offset(entry->pgd, address);
	if (entry->pud == 0) {
		printk(KERN_WARNING "pud is noooooooone\n");
		return true;
	}
	// todo:: to support thp, do some error checking here and see if a huge page is being allocated
	entry->pmd = pmd_offset(entry->pud, address);
	if (pmd_none(*(entry->pmd)) ||
		pud_large(*(entry->pud))) {
		if (pmd_none(*(entry->pmd)))
			printk(KERN_WARNING "pmd is noone %lx",
				   address);
		else
			printk(KERN_ERR "pud is a large page");
		entry->pmd = NULL;
		entry->pte = NULL;
		return true;
	}
	//todo:: pte_offset_map_lock<-- what is this? when whould I need to take a lock?
	entry->pte = pte_offset_map(entry->pmd, address);
	entry->initialized = true;
	return false;
}

/************************** TRACE RECORDING FOR MEMORY PREFETCHING BEGIN ********************************/
__always_inline void trace_maybe_set_pte(vm_t *entry, bool *return_early)
{
	//todo::  `taskset -c 1 ./mmap_random_rw 4 500000 1000000 w t` breaks the tracer
	//works fine for slightly smaller memory allocatons
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
	// accesssed just before faulting on this page, is not present in memory. Additionally,
	// we set the special bit (bit 58, see x86 manual) to later know that we are responsible
	// for the fault.
	pte_deref_value &= ~PRESENT_BIT_MASK;
	pte_deref_value |= SPECIAL_BIT_MASK;
	set_pte(entry->pte, native_make_pte(pte_deref_value));
}

static void drain_microset() {
	unsigned long i;
	for (i = 0; i != trace.microset_pos; i++) {
		trace.accesses[trace.pos++] = trace.microset[i].address & PAGE_ADDR_MASK;
		trace_clear_pte(&trace.microset[i]);
	}
	trace.microset_pos = 0;
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

static void do_unmap_5(pte_t *pte)
{
	unsigned long pte_deref_value = native_pte_val(*pte);
	if (pte_deref_value & SPECIAL_BIT_MASK) {
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		set_pte(pte, native_make_pte(pte_deref_value));
	}
}
static void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address, struct task_struct *tsk,
			    bool *return_early, int magic)
{
	if (unlikely(trace.accesses == NULL)) {
		printk(KERN_ERR "trace not initialized\n");
		return;
	}

	if (trace.process_pid == tsk->pid && trace.pos < TRACE_MAX_LEN) {
		vm_t* entry;
		struct mm_struct *mm = tsk->mm;
		down_read(&mm->mmap_sem);

		if (trace.microset_pos == trace.microset_size) {
			/* The microset is full. Start a new microset. */
			drain_microset();
		}

		BUG_ON(trace.microset_pos >= trace.microset_size);
		entry = &trace.microset[trace.microset_pos];
		if (vm_init(entry, mm, address)) {
			goto error_out;
		}
		trace.microset_pos++;
		//todo:: optimze later, to return here and maybe avoid tlb flush?
		trace_maybe_set_pte(entry, return_early);
#ifdef IN_ALT_PATTERN_CHECKS
		in_alt_pattern = check_alt_pattern(address, regs->ip);
		// todo:: investigate:: sometimes running the same tracing
		// second time gets rid of all alt pattern issues
		// maybe happening only after a fresh reboot. probably
		// something to do with faulting on INSTR addresses
		if (in_alt_pattern)
			trace.alt_pattern_counter++;

		if (!in_alt_pattern && exited_alt_pattern) {
			trace_clear_pte(&recent_accesses[1]);
			exited_alt_pattern = false;
		}
#endif // IN_ALT_PATTERN_CHECKS
		// if (likely(trace.entry.address !=
		// 		   trace.last_entry.address && //CoW?
		// 	   !in_alt_pattern)) {
		// 	trace_clear_pte(&trace.last_entry);
		// }

	error_out:
		get_cpu();
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
		// the following, if used correctly, can trace TLB count.
		// trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
		put_cpu();

		up_read(&mm->mmap_sem);

		// trace.last_entry = trace.entry;
		// trace.accesses[trace.pos++] = address & PAGE_ADDR_MASK;
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
/************************** TRACE RECORDING FOR MEMORY PREFETCHING END ********************************/
