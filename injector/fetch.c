#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/injections.h>

#include "common.h"
#include "fetch.h"
#include "kevictd.h"

#include <linux/kallsyms.h>

#define MAX_PRINT_LEN 768
#define MAX_NUM_THREADS 20
static const int FOOTSTEPPING_JUMP = 10;
static const int SYNC_THRESHHOLD = 10;

// TODO:: vmalloc() these for group leader thread
static DEFINE_SPINLOCK(key_page_indices_lock);
unsigned long long int key_page_indices[MAX_NUM_THREADS];
static atomic_long_t map_intent[MAX_NUM_THREADS] = {ATOMIC_INIT(0)};
static unsigned long *bufs[MAX_NUM_THREADS];
static size_t counts[MAX_NUM_THREADS];
static atomic_t num_threads = ATOMIC_INIT(0);

// todo:: this should be same as the eviction CPU once eviction work chunks
// are properly broken down to avoid head of line blocking for prefetching
// it seems even now using eviction CPU for fetching would be acceptable
// but it has ~10% performance overhead and I think fixing some eviction
// things will get rid of this down the line and as a temporary measure think
// it is better to use a separate CPU for fetching
static const int FETCH_OFFLOAD_CPU = 6;

// the number of pages ahead of our current tape position to start prefetching
#define GAP 200

// the length of tape over which we look for pages to prefetch
#define BATCH_LENGTH 100

// exported from mm/memory.c mm/internal.h function
int do_swap_page(struct vm_fault *vmf);
static int (*do_swap_page_p)(struct vm_fault *vmf);

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm,
			  struct vm_fault *vmf);

static unsigned long bump_next_fetch(unsigned long next_fetch,
				     unsigned long *buf, unsigned long len,
				     struct mm_struct *mm)
{
	while (likely(next_fetch < len)) {
		pte_t *pte = addr2pte(buf[next_fetch], mm);
		if (unlikely(pte && !pte_none(*pte) && pte_present(*pte))) {
			// page already mapped in page table
			next_fetch++;
			//fetch->already_present++;
		} else {
			int i = 0;
			for (i = 0; i < atomic_read(&num_threads); i++) {
				if (atomic_long_read(&map_intent[i]) ==
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

	return next_fetch < len ? next_fetch : len - 1;
}

static void prefetch_work_func(struct work_struct *work)
{
	struct prefetching_state *fetch =
		container_of(work, struct prefetching_state, prefetch_work);
	struct task_struct_oblivious *obl =
		container_of(fetch, struct task_struct_oblivious, fetch);
	struct task_struct *tsk = container_of(obl, struct task_struct, obl);

	unsigned long current_pos_idx;
	struct vm_fault vmf;
	unsigned long paddr;

	//q:: what is down_read? is it not necessary here?
	//down_read(&tsk->mm->mmap_sem);

	if (unlikely(!memtrace_getflag(TAPE_FETCH)))
		return;

	current_pos_idx = key_page_indices[obl->tind];
	if (fetch->prefetch_next_idx < current_pos_idx) {
		fetch->prefetch_next_idx = current_pos_idx;
	}

	for (; fetch->prefetch_next_idx < (current_pos_idx + GAP + BATCH_LENGTH) &&
	        fetch->prefetch_next_idx < fetch->tape_length;
	        fetch->prefetch_next_idx++) {
		paddr = fetch->tape[fetch->prefetch_next_idx];

		if (prefetch_addr(paddr, tsk->mm, &vmf)) {
			fetch->found_counter++;
		} else {
			fetch->already_present++;
		}
	}

	// TODO: Pre-fault from current_pos_idx to current_pos_idx + BATCH_LENGTH.

	key_page_indices[tsk->obl.tind] = bump_next_fetch(current_pos_idx + BATCH_LENGTH, fetch->tape, fetch->tape_length, tsk->mm);
	fetch->key_page_idx = key_page_indices[tsk->obl.tind]; // for debugging

	//up_read(&tsk->mm->mmap_sem);
}

void fetch_init_atomic(struct task_struct *tsk, unsigned long flags) {
	struct prefetching_state *fetch = &tsk->obl.fetch;

	BUG_ON(fetch->tape != NULL);

	tsk->obl.tind = atomic_inc_return(&num_threads) - 1;
	tsk->obl.flags = flags;

	memset(fetch, 0, sizeof(struct prefetching_state));
	fetch->tape_length = counts[tsk->obl.tind] / sizeof(void *);
	printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
	       counts[tsk->obl.tind], fetch->tape_length);

	fetch->tape = bufs[tsk->obl.tind];

	INIT_WORK(&fetch->prefetch_work, prefetch_work_func);
}

void fetch_init(struct task_struct *tsk, int flags)
{
	int thread_ind = 0;
	do_swap_page_p = (void *)kallsyms_lookup_name("do_swap_page");
	atomic_set(&num_threads, 0);
	memset(key_page_indices, 0, sizeof(key_page_indices));
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

		if (filesize == 0 || count == 0) {
			printk(KERN_ERR "unable to initialize fetching\n");
			vfree(buf);
			return;
		}

		bufs[thread_ind] = buf;
		counts[thread_ind] = count;
	}

	fetch_init_atomic(tsk, flags);
}

void fetch_clone(struct task_struct *p, unsigned long clone_flags)
{

	if (memtrace_getflag(ONE_TAPE)) {
		//todo:: do something with this var
		p->obl = current->obl;
		p->obl.tind = atomic_inc_return(&num_threads) - 1;
	} else {
		// p->obl.tind is set by the function below
		fetch_init_atomic(p, current->obl.flags);
	}
}

void fetch_fini(struct task_struct *tsk)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	if (fetch->tape != NULL) {
		int num_threads_left = atomic_dec_return(&num_threads);
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

		if (memtrace_getflag(ONE_TAPE)) {
			if (num_threads_left != 0)
				return;
		}
		vfree(fetch->tape);
		fetch->tape = NULL; // todo:: should not need once in kernel
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
void fetch_page_fault_handler(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address, struct task_struct *tsk,
			      bool *return_early, int magic)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
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

	spin_lock_irqsave(&key_page_indices_lock, flags);
	atomic_long_set(&map_intent[tsk->obl.tind], address & PAGE_ADDR_MASK);

	for (i = 0; i < atomic_read(&num_threads); i++) {
		//TODO:: use fetch->tape-like thing
		if (i != tsk->obl.tind &&
		    (bufs[i][key_page_indices[i]] & PAGE_ADDR_MASK) ==
			    (address & PAGE_ADDR_MASK)) {
			key_page_indices[i] = bump_next_fetch(
				key_page_indices[i] + FOOTSTEPPING_JUMP, bufs[i],
				counts[i] / sizeof(void *), tsk->mm);
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
	    (fetch->tape[key_page_indices[tsk->obl.tind]] & PAGE_ADDR_MASK) ==
		    (address & PAGE_ADDR_MASK)) {
		if (memtrace_getflag(OFFLOAD_FETCH))
			queue_work_on(FETCH_OFFLOAD_CPU, system_highpri_wq,
				      &fetch->prefetch_work);
		else
			prefetch_work_func(&fetch->prefetch_work);
	}

	spin_unlock_irqrestore(&key_page_indices_lock, flags);
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
