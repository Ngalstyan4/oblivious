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

// todo:: this should be same as the eviction CPU once eviction work chunks
// are properly broken down to avoid head of line blocking for prefetching
// it seems even now using eviction CPU for fetching would be acceptable
// but it has ~10% performance overhead and I think fixing some eviction
// things will get rid of this down the line and as a temporary measure think
// it is better to use a separate CPU for fetching
static const int FETCH_OFFLOAD_CPU = 6;

static struct process_state *process_state_new() {
	struct process_state *proc = (struct process_state *)vmalloc(sizeof(struct process_state));
	int i;
	if (proc == NULL) return NULL;
	spin_lock_init(&proc->next_fetches_lock);
	atomic_set(&proc->thread_pos, 0);
	for (i = 0; i < OBL_MAX_NUM_THREADS; i++) {
		proc->next_fetches[i] = 0;
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
			//printk(KERN_WARNING
			//       "Unused memory buffer left on process state. "
			//       "Freeing to avoid memory leak\n");
			vfree(proc->bufs[i]);
		}

	vfree(proc);
}

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm,
			  struct vm_fault *vmf);

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
	struct process_state *proc = tsk->group_leader->obl.proc;

	int i = 0;
	int num_prefetch = 0;
	struct mm_struct *mm = tsk->mm;
	struct vm_fault vmf;
	unsigned long paddr;
	//q:: what is down_read? is it not necessary here?
	//down_read(&mm->mmap_sem);

	unsigned long fetch_start = fetch->next_fetch;
	if (fetch_start != proc->next_fetches[obl->tind]) {
		// TODO:: add footstepping counter
		fetch_start = fetch->next_fetch = proc->next_fetches[obl->tind];
	}

	if (unlikely(!memtrace_getflag(TAPE_FETCH)))
		return;
	for (i = 0; i < 100 && num_prefetch < 50; i++) {
		if (unlikely(fetch_start + i >= fetch->num_accesses)) {
			i--;
			break;
		}
		paddr = fetch->accesses[fetch_start + i];

		if (prefetch_addr(paddr, mm, &vmf) == true) {
			num_prefetch++;
			//do_swap_page_p(&vmf);
		} else {
			fetch->already_present++;
		}
	}

	fetch->found_counter += num_prefetch;
	//lru_add_drain();// <Q::todo:: what does this do?.. Push any new pages onto the LRU now
	fetch->next_fetch += i;

	fetch->next_fetch = bump_next_fetch(fetch->next_fetch, fetch->accesses,
					    fetch->num_accesses,
					    proc->map_intent, atomic_read(&proc->thread_pos),
					    mm);
	proc->next_fetches[tsk->obl.tind] = fetch->next_fetch;
	//up_read(&mm->mmap_sem);
}

void fetch_init_atomic(struct task_struct *tsk, unsigned long flags) {
	struct prefetching_state *fetch = &tsk->obl.fetch;
	struct process_state *proc = tsk->group_leader->obl.proc;

	BUG_ON(fetch->accesses != NULL);

	tsk->obl.tind = atomic_inc_return(&proc->thread_pos) - 1;
	tsk->obl.flags = flags;

	memset(fetch, 0, sizeof(struct prefetching_state));
	fetch->num_accesses = proc->counts[tsk->obl.tind] / sizeof(void *);
	printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
	       proc->counts[tsk->obl.tind], fetch->num_accesses);

	fetch->accesses = proc->bufs[tsk->obl.tind];

	INIT_WORK(&fetch->prefetch_work, prefetch_work_func);
	fetch->prefetch_start = true; // can be used to pause and resume
}

void fetch_init(struct task_struct *tsk, int flags)
{
	// this function is called ONLY by the group leader in a multithreaded setting.
	// the task struct corresponding to the group leader keeps per-process state
	int thread_ind = 0;
	struct process_state *proc = NULL;
	current->group_leader->obl.proc = process_state_new();
	proc = current->group_leader->obl.proc;

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

		count = read_trace(trace_filepath, (char *)buf, filesize);

		if (filesize == 0 || count == 0) {
			printk(KERN_ERR "unable to initialize fetching\n");
			vfree(buf);
			return;
		}

		proc->bufs[thread_ind] = buf;
		proc->counts[thread_ind] = count;
	}

	fetch_init_atomic(tsk, flags);
}

void fetch_clone(struct task_struct *p, unsigned long clone_flags)
{
	// **note the group_leader**
	struct process_state *proc = current->group_leader->obl.proc;

	if (memtrace_getflag(ONE_TAPE)) {
		//todo:: do something with this var
		p->obl = current->obl;
		p->obl.tind = atomic_inc_return(&proc->thread_pos) - 1;
	} else {
		// p->obl.tind is set by the function below
		fetch_init_atomic(p, current->obl.flags);
	}
}

void fetch_fini(struct task_struct *tsk)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	struct process_state *proc = tsk->group_leader->obl.proc;

	if (fetch->accesses != NULL) {
		int num_threads_left = atomic_dec_return(&proc->thread_pos);
		struct vm_area_struct *last_vma =
			find_vma(tsk->mm, fetch->accesses[fetch->next_fetch]);

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
		       fetch->num_accesses - fetch->next_fetch <
				       SYNC_THRESHHOLD ?
			       "SYNC" :
			       "!!LOST!!",
		       fetch->next_fetch, fetch->accesses[fetch->next_fetch],
		       last_vma->vm_start, last_vma->vm_end,
		       fetch->num_accesses);

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
	struct process_state *proc = tsk->group_leader->obl.proc;
	unsigned long flags;
	int i;

	if (unlikely(fetch->accesses == NULL)) {
		printk(KERN_ERR "fetch trace not initialized\n");
		return;
	}

	if (unlikely(error_code & PF_INSTR)) {
		//printk(KERN_ERR "instr fault %lx", address);
		return;
	}
	fetch->num_fault++;

	spin_lock_irqsave(&proc->next_fetches_lock, flags);
	atomic_long_set(&proc->map_intent[tsk->obl.tind], address & PAGE_ADDR_MASK);

	for (i = 0; i < atomic_read(&proc->thread_pos); i++) {
		//TODO:: use fetch->accesses-like thing
		if (i != tsk->obl.tind &&
		    (proc->bufs[i][proc->next_fetches[i]] & PAGE_ADDR_MASK) ==
			    (address & PAGE_ADDR_MASK)) {
			proc->next_fetches[i] = bump_next_fetch(
				proc->next_fetches[i] + FOOTSTEPPING_JUMP, proc->bufs[i],
				proc->counts[i] / sizeof(void *),
			    	proc->map_intent, atomic_read(&proc->thread_pos),
				tsk->mm);
		}
	}

	/* first condition is relevant in the very beginning
	 *TODO:: [next line is out of date] remove ->pos from fetch
	 * --second condition uses the above syncing to follow the tape--
	 * third condition uses address comparison to stay in sync
	 *
	 * in single core prefetching the second condition is not needed
	 * and direct addr comparision is enough to stay in sync
	 * However, in multicore prefetching it is common for an address to
	 * be in tape of one thread but be fetched by another, therefore relying
	 * on direct addr comparision clues is not reliable and the third condition
	 * is usually never satisfied
	*/
	if (fetch->next_fetch == 0 || fetch->pos >= fetch->next_fetch ||
	    (fetch->accesses[fetch->next_fetch] & PAGE_ADDR_MASK) ==
		    (address & PAGE_ADDR_MASK) ||
	    (fetch->accesses[proc->next_fetches[tsk->obl.tind]] & PAGE_ADDR_MASK) ==
		    (address & PAGE_ADDR_MASK)) {
		if (memtrace_getflag(OFFLOAD_FETCH))
			queue_work_on(FETCH_OFFLOAD_CPU, system_highpri_wq,
				      &fetch->prefetch_work);
		else
			prefetch_work_func(&tsk->obl.fetch.prefetch_work);
	}

	spin_unlock_irqrestore(&proc->next_fetches_lock, flags);
}

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm,
			  struct vm_fault *vmf)
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

	vmf->address = addr;
	vmf->vma = vma;
	vmf->pmd = pmd;
	vmf->pte = pte;
	vmf->orig_pte = pte_val;
	vmf->ptl = pte_lockptr(mm, pmd);
	vmf->flags = FAULT_FLAG_USER; // <-- todo::verify, improvising here

	return true;
}
/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE END ++++++++++++++++++++++*/
