#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>

#include <linux/injections.h>

#include "common.h"
#include "record.h"
#include "mem_pattern_trace.h"

#define TRACE_ARRAY_SIZE 1024 * 1024 * 1024 * 1ULL
#define TRACE_MAX_LEN (TRACE_ARRAY_SIZE / sizeof(void *))

int limit = 200000;

/*
typedef struct {
	pid_t process_pid;
	struct mm_struct *mm;
	unsigned long *accesses;
	unsigned long pos;
	char filepath[FILEPATH_LEN];

	unsigned long microset_size;
	unsigned long microset_pos;
	unsigned long *microset;
} tracing_state;
static tracing_state trace;

*/
static void do_unmap_5(pte_t *pte);
static void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address, struct task_struct *tsk,
			    bool *return_early, int magic);

void record_init(struct task_struct *tsk, int flags, unsigned int microset_size)
{
	struct trace_recording_state *record = &tsk->obl.record;

	// in case a prvious recording was sigkilled before FINI call
	record_fini(tsk);

	memset(record, 0, sizeof(struct trace_recording_state));
	tsk->obl.flags = flags;

	record->accesses = vmalloc(TRACE_ARRAY_SIZE);
	if (record->accesses == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		return;
	}

	record->microset_size = microset_size;
	record->microset_pos = 0;
	record->microset =
		vmalloc(record->microset_size * sizeof(unsigned long));
	if (record->microset == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		vfree(record->accesses);
		record->accesses = NULL;
		return;
	}
	memset(record->microset, 0x00,
	       record->microset_size * sizeof(unsigned long));

	set_pointer(5, do_unmap_5);
	set_pointer(6, do_unmap_5); //<-- for handle_pte_fault
	set_pointer(2, do_page_fault_2);
}

void record_init1(struct task_struct *tsk, int flags, unsigned int microset_size)
{
	struct trace_recording_state *record = &tsk->obl.record;

	// in case a prvious recording was sigkilled before FINI call
	//record_fini(tsk);

	memset(record, 0, sizeof(struct trace_recording_state));
	tsk->obl.flags = flags;

	record->accesses = vmalloc(TRACE_ARRAY_SIZE);
	if (record->accesses == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		return;
	}

	record->microset_size = microset_size;
	record->microset_pos = 0;
	record->microset =
		vmalloc(record->microset_size * sizeof(unsigned long));
	if (record->microset == NULL) {
		printk(KERN_ERR "Unable to allocate memory for tracing\n");
		vfree(record->accesses);
		record->accesses = NULL;
		return;
	}
	memset(record->microset, 0x00,
	       record->microset_size * sizeof(unsigned long));

}

static void drain_microset();

bool record_initialized(struct task_struct *tsk)
{
	return tsk->obl.record.accesses != NULL;
}

void record_clone(struct task_struct *p, unsigned long clone_flags)
{

	record_init1(p, current->obl.flags, current->obl.record.microset_size);
	//struct task_struct *me = current;
	//struct task_struct *t = me;
	//do {
	//	printk(KERN_ERR "record clone info: %d", t->pid);
	//}
	//while_each_thread(me, t);
}

void record_fini(struct task_struct *tsk)
{
	struct trace_recording_state *record = &tsk->obl.record;

	if (record_initialized(tsk)) {
		char trace_filepath[FILEPATH_LEN];
		int thread_ind = tsk->pid - tsk->group_leader->pid;
		snprintf(trace_filepath, FILEPATH_LEN, RECORD_FILE_FMT,
			 tsk->comm, thread_ind);

		printk(KERN_ERR "for pid %d writing at %s\n", tsk->pid, trace_filepath);
		// in case path is too long, truncate;
		trace_filepath[FILEPATH_LEN - 1] = '\0';
		down_read(&tsk->mm->mmap_sem);
		drain_microset();
		up_read(&tsk->mm->mmap_sem);
		if (record->pos >= TRACE_MAX_LEN) {
			printk(KERN_ERR "Ran out of buffer space");
			printk(KERN_ERR "Proc mem pattern not fully recorded\n"
					"please increase buffer "
					"size(TRACE_ARRAY_SIZE) and rerun\n");
		}

		write_trace(trace_filepath, (const char *)record->accesses,
			    record->pos * sizeof(void *));

		vfree(record->microset);
		vfree(record->accesses);
		// make sure next call to record_initialized() will return false;
		record->accesses = NULL;
	}
}

// in case we faulted while tracing and therefore trace_fini() was never called, record_force_clean can be called
// upon module cleanup
void record_force_clean()
{
	struct trace_recording_state *record = &current->obl.record;
	if (record->accesses) {
		vfree(record->microset);
		vfree(record->accesses);
		record->accesses = NULL;
	}
}

/************************** TRACE RECORDING FOR MEMORY PREFETCHING BEGIN ********************************/
__always_inline void trace_maybe_set_pte(pte_t *pte, bool *return_early)
{
	unsigned long pte_deref_value;
	*return_early = false;

	if (unlikely(pte == NULL))
		return;
	pte_deref_value = native_pte_val(*pte);
	if (pte_deref_value & SPECIAL_BIT_MASK) {
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		set_pte(pte, native_make_pte(pte_deref_value));
		*return_early = true;
	}
}

// used to unmap the last entry
static void trace_clear_pte(pte_t *pte)
{
	unsigned long pte_deref_value;
	// if previous fault was a pmd allocation fault, we will not have pte
	if (unlikely(pte == NULL))
		return;
	pte_deref_value = native_pte_val(*pte);
	// normally, there would just be allocation faults. but for tracing we want to see *all* page
	// accesses so we make sure that form kernel's point of view the page that the application
	// accesssed just before faulting on this page, is not present in memory. Additionally,
	// we set the special bit (bit 58, see x86 manual) to later know that we are responsible
	// for the fault.
	pte_deref_value &= ~PRESENT_BIT_MASK;
	pte_deref_value |= SPECIAL_BIT_MASK;
	set_pte(pte, native_make_pte(pte_deref_value));
}

static void drain_microset()
{
	struct trace_recording_state *record = &current->obl.record;
	unsigned long i;
	for (i = 0; i != record->microset_pos && record->pos < TRACE_MAX_LEN;
	     i++) {
		// microset already records pages with 12 bit in-page offset cleared
		record->accesses[record->pos++] = record->microset[i];

		trace_clear_pte(addr2pte(record->microset[i], current->mm));
	}
	record->microset_pos = 0;
}

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

	struct trace_recording_state *record = &current->obl.record;

	BUG_ON(tsk != current);
	*return_early = false;

	/* Don't track instruction pages. */
	if (unlikely(error_code & PF_INSTR)) {
		return;
	}

	if (current->obl.flags & TRACE_RECORD && record->pos < TRACE_MAX_LEN) {
		down_read(&current->mm->mmap_sem);

		if (record->microset_pos == record->microset_size) {
			/* The microset is full. Start a new microset. */
			drain_microset();
		}

		BUG_ON(record->microset_pos >= record->microset_size);
		record->microset[record->microset_pos] =
			address & PAGE_ADDR_MASK;
		record->microset_pos++;

		//todo:: optimze later, to return here and maybe avoid tlb flush?
		trace_maybe_set_pte(addr2pte(address, current->mm),
				    return_early);

		get_cpu();
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
		// the following, if used correctly, can trace TLB count.
		// trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
		put_cpu();

		up_read(&current->mm->mmap_sem);
	}
}
EXPORT_SYMBOL(do_page_fault_2);
/************************** TRACE RECORDING FOR MEMORY PREFETCHING END ********************************/
