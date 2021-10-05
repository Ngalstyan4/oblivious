#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/injections.h>

#include "common.h"
#include "fetch.h"
#include "kevictd.h"

#include <linux/kallsyms.h>

#define OBL_MAX_PRINT_LEN 768
static const int FOOTSTEPPING_JUMP = 10;
static const int SYNC_THRESHHOLD = 10;

#define COLLECT_STATS 0

// todo:: this should be same as the eviction CPU once eviction work chunks
// are properly broken down to avoid head of line blocking for prefetching
// it seems even now using eviction CPU for fetching would be acceptable
// but it has ~10% performance overhead and I think fixing some eviction
// things will get rid of this down the line and as a temporary measure think
// it is better to use a separate CPU for fetching
static const int FETCH_OFFLOAD_CPU = 6;

// the number of pages ahead of our current tape position to start prefetching
#define LOOKAHEAD 400

// the length of tape over which we look for pages to prefetch
#define BATCH_LENGTH 100

// exported from mm/memory.c mm/internal.h function
int do_swap_page_prefault_3po(struct vm_fault *vmf);

static struct process_state *process_state_new() {
	struct process_state *proc = (struct process_state *)vmalloc(sizeof(struct process_state));
	int i;
	if (proc == NULL) return NULL;
	spin_lock_init(&proc->key_page_indices_lock);
	atomic_set(&proc->num_threads, 0);
	for (i = 0; i < OBL_MAX_NUM_THREADS; i++) {
		proc->key_page_indices[i] = 0;
		atomic_long_set(&proc->map_intent[i], 0);
		proc->bufs[i] = NULL;
		proc->counts[i] = 0;
	}
	return proc;
}

static void process_state_free(struct process_state *proc)
{
	int i;
	for (i = 0; i < OBL_MAX_NUM_THREADS; i++)
		if (proc->bufs[i] != NULL) {
			// initially wanted to free the buffer as soon as the corresponding thread ends
			// that would be the right hting todo. But hte way map_intents are implemented now,
			// we would still look through all ever-allocated buffers for footstepping collisions
			// and if we freed these buffers earlier, such footsteppig would result in use after free
			// todo:: maybe implement smarted map_intent that marks threads as done and no longer checks
			// them for footstepping? maybe not.

			//printk(KERN_WARNING
			//       "Unused memory buffer left on process state. "
			//       "Freeing to avoid memory leak\n");
			vfree(proc->bufs[i]);
		}

	vfree(proc);
}

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm);
static bool prefault_addr(unsigned long addr, struct mm_struct *mm);

static unsigned long bump_next_fetch(unsigned long next_fetch,
				     unsigned long *buf, unsigned long buf_len,
				     atomic_long_t *map_intents,
				     unsigned long map_intents_len,
				     struct mm_struct *mm)
{
	while (likely(next_fetch < buf_len)) {
		pte_t *pte = addr2pte(buf[next_fetch], mm);
		if (unlikely(pte && !pte_none(*pte) && pte_present(*pte))) {
			// page already mapped in page table
			next_fetch++;
			//fetch->already_present++;
		} else {
			int i = 0;
			for (i = 0; i < map_intents_len; i++) {
				if (atomic_long_read(&map_intents[i]) ==
					buf[next_fetch] &&
				    next_fetch != 0) {
					printk(KERN_INFO
					       "MAP INTENT INVARIANT TRIGGERED "
					       "tind %d",
					       i);
					next_fetch++;
					continue;
				}
			}
			break;
		}
	}

	return next_fetch < buf_len ? next_fetch : buf_len - 1;
}

static void prefetch_work_func(struct work_struct *work)
{
	struct prefetching_state *fetch =
		container_of(work, struct prefetching_state, prefetch_work);
	struct task_struct_oblivious *obl =
		container_of(fetch, struct task_struct_oblivious, fetch);
	struct task_struct *tsk = container_of(obl, struct task_struct, obl);
	struct process_state *proc = tsk->obl.proc;

	unsigned long current_pos_idx;
	unsigned long i;
	unsigned long start_ahead;

	if (unlikely(!memtrace_getflag(TAPE_FETCH)))
		return;

	down_read(&tsk->mm->mmap_sem);

	current_pos_idx = proc->key_page_indices[obl->tind];
	if (fetch->prefetch_next_idx < current_pos_idx) {
		fetch->prefetch_next_idx = current_pos_idx;
	}
	start_ahead = fetch->prefetch_next_idx - current_pos_idx;

	/*
	 * Prefetch from where we left off until current_pos_idx + LOOKAHEAD +
	 * BATCH_LENGTH. We probably left off around current_pos_idx + LOOKAHEAD,
	 */
	for (; fetch->prefetch_next_idx < (current_pos_idx + LOOKAHEAD + BATCH_LENGTH) &&
	        fetch->prefetch_next_idx < fetch->tape_length;
	        fetch->prefetch_next_idx++) {
		unsigned long paddr = fetch->tape[fetch->prefetch_next_idx];

		if (prefetch_addr(paddr, tsk->mm)) {
			fetch->found_counter++;
		} else {
			fetch->already_present++;
		}
	}

	/* Pre-fault from current_pos_idx + 1 to current_pos_idx + BATCH_LENGTH. */
	for (i = 1; i < BATCH_LENGTH && i < start_ahead; i++) {
		unsigned long addr = fetch->tape[current_pos_idx + i];
		prefault_addr(addr, tsk->mm);
	}

	proc->key_page_indices[tsk->obl.tind] =
	    bump_next_fetch(current_pos_idx + BATCH_LENGTH, fetch->tape,
			    fetch->tape_length,
			    proc->map_intent, atomic_read(&proc->num_threads),
			    tsk->mm);
	fetch->key_page_idx = proc->key_page_indices[tsk->obl.tind]; // for debugging

	up_read(&tsk->mm->mmap_sem);

	#if COLLECT_STATS
	{
		u64 last_key_page_num_fetched = fetch->last_key_page_num_fetched;
		u64 last_key_page_time = fetch->last_key_page_time;
		u64 curr_key_page_num_fetched = fetch->found_counter - fetch->last_key_page_total_fetched;
		u64 curr_key_page_time = ktime_get_real_ns();

		fetch->last_key_page_num_fetched = curr_key_page_num_fetched;
		fetch->last_key_page_time = curr_key_page_time;
		fetch->last_key_page_total_fetched = fetch->found_counter;

		if (likely(last_key_page_time != 0)) {
			u64 elapsed = curr_key_page_time - last_key_page_time;
			u64 pages_per_sec = 1000000000ull * last_key_page_num_fetched / elapsed;

			stats_event(&tsk->obl.fetch.timing_stats, elapsed);
			stats_event(&tsk->obl.fetch.batch_stats, curr_key_page_num_fetched);
			stats_event(&tsk->obl.fetch.bandwidth_stats, pages_per_sec);
		}
	}
	#endif
}

void fetch_init_atomic(struct task_struct *tsk, struct process_state *proc, unsigned long flags) {
	struct prefetching_state *fetch = &tsk->obl.fetch;

	/*
	 * samkumar: There might be a race condition here. What if, before we
	 * increment the reference count, the other thread gets to run and it
	 * exits decrementing the reference count and then freeing the structure?
	 *
	 * A better solution would be to have the parent thread increment the
	 * reference count, so there's no chance for it to be freed before the
	 * child thread runs. Or, block the parent until the child has
	 * incremented the reference count.
	 */
	tsk->obl.proc = proc;

	BUG_ON(fetch->tape != NULL);

	tsk->obl.tind = atomic_inc_return(&proc->num_threads) - 1;
	tsk->obl.flags = flags;

	memset(fetch, 0, sizeof(struct prefetching_state));
	fetch->tape_length = proc->counts[tsk->obl.tind] / sizeof(void *);
	printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
	       proc->counts[tsk->obl.tind], fetch->tape_length);

	fetch->tape = proc->bufs[tsk->obl.tind];

	INIT_WORK(&fetch->prefetch_work, prefetch_work_func);
}

void fetch_init(struct task_struct *tsk, int flags)
{
	// this function is called ONLY by the first thread in a multithreaded setting.
	// its task struct keeps per-process state
	int thread_ind = 0;
	struct process_state *proc = process_state_new();

	if (proc == NULL) {
		printk(
		    KERN_ERR
		    "Unable to allocate memory for additional process state\n");
		return;
	}

	for (;; thread_ind++) {
		char trace_filepath[FILEPATH_LEN];
		size_t filesize = 0;
		unsigned long *buf;
		size_t count;

		snprintf(trace_filepath, FILEPATH_LEN, FETCH_FILE_FMT,
			 tsk->comm, thread_ind);

		// in case path is too long, truncate;
		trace_filepath[FILEPATH_LEN - 1] = '\0';

		if (!file_exists(trace_filepath)) {
			printk(KERN_INFO "fetch: successfully read %d tapes\n",
			       thread_ind);
			break;
		}

		filesize = file_size(trace_filepath);
		if (filesize == 0) {
			printk(KERN_WARNING "fetch: read empty at %s", trace_filepath);
			return;
		}

		buf = vmalloc(filesize);
		if (buf == NULL) {
			printk(KERN_ERR "unable to allocate memory for reading "
					"the trace\n");
			return;
		}

		count = read_tape(trace_filepath, (char *)buf, filesize);

		if (filesize != count) {
			printk(KERN_ERR "unable to initialize fetching\n");
			vfree(buf);
			return;
		}

		proc->bufs[thread_ind] = buf;
		proc->counts[thread_ind] = count;
	}

	fetch_init_atomic(tsk, proc, flags);
}

void fetch_clone(struct task_struct *p, unsigned long clone_flags)
{
	struct process_state *proc = current->obl.proc;

	if (memtrace_getflag(ONE_TAPE)) {
		//todo:: do something with this var
		p->obl = current->obl;
		p->obl.tind = atomic_inc_return(&proc->num_threads) - 1;
	} else {
		// p->obl.tind is set by the function below
		fetch_init_atomic(p, proc, current->obl.flags);
	}
}

void fetch_fini(struct task_struct *tsk)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	struct process_state *proc = tsk->obl.proc;
	if (fetch->tape != NULL) {
		int num_threads_left = atomic_dec_return(&proc->num_threads);
		struct vm_area_struct *last_vma =
			find_vma(tsk->mm, fetch->tape[fetch->key_page_idx]);

		cancel_work_sync(&fetch->prefetch_work);
		printk(KERN_INFO
		       "tind %d: found %d/%d "
		       "min:%ld, maj: %ld "
		       "alrdy prsnt: %d %s\n"
		       "\t(nextind %ld next 0x%lx vm_start 0x%lx vm_end) "
		       "0x%lx num_accesses %ld\n",
		       tsk->obl.tind, fetch->found_counter, fetch->num_fault,
		       current->min_flt, current->maj_flt,
		       fetch->already_present,
		       fetch->tape_length - fetch->key_page_idx <
				       SYNC_THRESHHOLD ?
			       "SYNC" :
			       "!!LOST!!",
		       fetch->key_page_idx, fetch->tape[fetch->key_page_idx],
		       last_vma->vm_start, last_vma->vm_end,
		       fetch->tape_length);

#if COLLECT_STATS
		stats_tell(&fetch->timing_stats, "KEY_PAGE_INTVL");
		stats_tell(&fetch->batch_stats, "FETCHED_PER_BATCH");
		stats_tell(&fetch->bandwidth_stats, "PAGES_PER_SEC");
#endif

		if (memtrace_getflag(ONE_TAPE)) {
			if (num_threads_left != 0)
				return;
		}

		if (num_threads_left == 0) {
			process_state_free(proc);
		}
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
void fetch_page_fault_handler(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address, struct task_struct *tsk,
			      bool *return_early, int magic)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	struct process_state *proc = tsk->obl.proc;
	unsigned long flags;
	int i;

	if (unlikely(fetch->tape == NULL)) {
		printk(KERN_ERR "fetch trace not initialized\n");
		return;
	}

	if (unlikely(error_code & PF_INSTR)) {
		//printk(KERN_ERR "instr fault %lx", address);
		return;
	}
	fetch->num_fault++;

	spin_lock_irqsave(&proc->key_page_indices_lock, flags);
	atomic_long_set(&proc->map_intent[tsk->obl.tind], address & PAGE_ADDR_MASK);

	for (i = 0; i < atomic_read(&proc->num_threads); i++) {
		//TODO:: use fetch->tape-like thing
		if (i != tsk->obl.tind &&
		    (proc->bufs[i][proc->key_page_indices[i]] & PAGE_ADDR_MASK) ==
			    (address & PAGE_ADDR_MASK)) {
			proc->key_page_indices[i] = bump_next_fetch(
				proc->key_page_indices[i] + FOOTSTEPPING_JUMP, proc->bufs[i],
				proc->counts[i] / sizeof(void *),
				proc->map_intent, atomic_read(&proc->num_threads),//todo::look at me again! thread_pos->num_threads
				tsk->mm);
		}
	}

	/* first condition is relevant in the very beginning
	 * second condition uses address comparison to stay in sync
	 *
	 * in single core prefetching the second condition is not needed
	 * and direct addr comparision is enough to stay in sync
	 * However, in multicore prefetching it is common for an address to
	 * be in tape of one thread but be fetched by another, therefore relying
	 * on direct addr comparision clues is not reliable and the third condition
	 * is usually never satisfied
	*/
	if (fetch->key_page_idx == 0 ||
	    (fetch->tape[proc->key_page_indices[tsk->obl.tind]] & PAGE_ADDR_MASK) ==
		    (address & PAGE_ADDR_MASK)) {
		if (memtrace_getflag(OFFLOAD_FETCH))
			queue_work_on(FETCH_OFFLOAD_CPU, system_highpri_wq,
				      &fetch->prefetch_work);
		else
			prefetch_work_func(&fetch->prefetch_work);
	}

	spin_unlock_irqrestore(&proc->key_page_indices_lock, flags);
}

static bool prefault_addr(unsigned long addr, struct mm_struct *mm)
{
	/* Create a "fake" page fault on this address, then call do_swap_page. */
	struct vm_fault vmf;
	int rv;
	pmd_t *pmd;
	pte_t *pte = addr2ptepmd(addr, mm, &pmd);

	/* PTE should exist, since swap cache key is stored in the PTE. */
	if (unlikely(pte == NULL)) {
		return false;
	}

	/* If PTE is already present, then there's nothing to do. */
	if (pte_present(*pte)) {
		return false;
	}

	/*
	 * Initialize vmf, similar to how __handle_mm_fault and handle_pte_fault
	 * would, focusing on those fields used by do_swap_page. The fields used
	 * by do_swap_page are vma, pmd, pte, orig_pte, address, and flags. It
	 * also uses ptl, but doesn't require that to be initialized going into
	 * this function; it sets it via pte_offset_map_lock.
	 */

	memset(&vmf, 0x00, sizeof(struct vm_fault));
	vmf.address = addr;
	vmf.vma = find_vma(mm, addr);
	vmf.pmd = pmd;
	vmf.pte = pte;
	vmf.orig_pte = *pte;
	vmf.flags = FAULT_FLAG_USER | FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_RETRY_NOWAIT; // any other flags?

	rv = do_swap_page_prefault_3po(&vmf);
	return (rv & (VM_FAULT_OOM | VM_FAULT_SIGBUS | VM_FAULT_RETRY | VM_FAULT_MAJOR | VM_FAULT_HWPOISON)) == 0;
}

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm)
{
	struct page *page;
	bool allocated = false;
	struct vm_area_struct *vma;
	pmd_t *pmd;
	pte_t *pte;
	pte_t pte_val;
	swp_entry_t rmem_entry;
	pte = addr2ptepmd(addr, mm, &pmd);
	if (!pte)
		return false;
	// prefetch the page if needed
	pte_val = *pte;
	if (pte_none(pte_val))
		return false;
	if (pte_present(pte_val))
		return false;
	rmem_entry = pte_to_swp_entry(pte_val);
	if (unlikely(non_swap_entry(rmem_entry)))
		return false;

	vma = find_vma(mm, addr);
	// here addr only used in interleave_nid() call to choose a
	// memory node for the page. it seems it is used to offer
	// some kind of locality/loadBalancing??
	// the point is, it does not have to be the precise addr we
	// intend to map the page on
	// I think gfp_mask should always be GFP_HIGHUSER_MOVABLE  in here

	// todo:: kernel doc Documentation/vm/unevictable-lru.txt talks about interaction of cgroups with
	// pages that are mlocked or otherwise marked as unevictable. It also talks about page_evictable()
	// as well as mechanisms to mark a *whole address space* as unevictable. perhaps we can use this
	// mechanism to prefetch into a separate address space that is not evictable to make sure
	// 1) the kernel does not meddle with prefetching
	// 2) we do not stall the kernel trying to evict things from our prefetched list that we are sure it is not
	// going to succeed.

	page = __read_swap_cache_async(rmem_entry, GFP_HIGHUSER_MOVABLE, vma,
				       addr, &allocated);
	if (!page)
		return false;

	if (!allocated) {
		put_page(page); //= page_cache_release
		return false;
	}
	swap_readpage(page);
	SetPageReadahead(page);
	if (memtrace_getflag(MARK_UNEVICTABLE))
		SetPageUnevictable(page);

	put_page(page); //= page_cache_release

	return true;
}
/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE END ++++++++++++++++++++++*/
