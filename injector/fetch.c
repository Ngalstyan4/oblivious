
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
const int MAX_SEARCH_DIST = 20;

// todo:: this should be same as the eviction CPU once eviction work chunks
// are properly broken down to avoid head of line blocking for prefetching
// it seems even now using eviction CPU for fetching would be acceptable
// but it has ~10% performance overhead and I think fixing some eviction
// things will get rid of this down the line and as a temporary measure think
// it is better to use a separate CPU for fetching
const int FETCH_OFFLOAD_CPU = 6;

// exported from mm/memory.c mm/internal.h function
int do_swap_page(struct vm_fault *vmf);
static int (*do_swap_page_p)(struct vm_fault *vmf);
//todo::
// dangerous!! probably need to protect by locks/atomics? but seems to work for now..
char fetch_print_buf[MAX_PRINT_LEN];
char *fetch_buf_end = fetch_print_buf;

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm,
			  struct vm_fault *vmf);

static void prefetch_work_func(struct work_struct *work)
{
	struct prefetching_state *fetch =
		container_of(work, struct prefetching_state, prefetch_work);
	struct task_struct_oblivious *obl =
		container_of(fetch, struct task_struct_oblivious, fetch);
	struct task_struct *tsk = container_of(obl, struct task_struct, obl);

	int i = 0;
	int num_prefetch = 0;
	struct mm_struct *mm = tsk->mm;
	struct vm_fault vmf;
	unsigned long paddr;
	pte_t *pte;
	//q:: what is down_read? is it not necessary here?
	//down_read(&mm->mmap_sem);
	unsigned long fetch_start = fetch->next_fetch;
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
		}
	}

	fetch->found_counter += num_prefetch;
	//lru_add_drain();// <Q::todo:: what does this do?.. Push any new pages onto the LRU now
	fetch->next_fetch += i;

	while (likely(fetch->next_fetch < fetch->num_accesses)) {
		pte = addr2pte(fetch->accesses[fetch->next_fetch], mm);
		if (unlikely(pte && !pte_none(*pte) && pte_present(*pte))) {
			// page already mapped in page table
			fetch->next_fetch++;
			fetch->already_present++;
		} else
			break;
	}
	//up_read(&mm->mmap_sem);
}

// todo:: move to task_struct_oblivious on next kernel recompile
static unsigned long *bufs[10];
static size_t counts[10];
static atomic_t thread_pos = ATOMIC_INIT(0);

void fetch_init_atomic(struct task_struct *tsk, unsigned long flags)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;

	int id = atomic_inc_return(&thread_pos) - 1;

	fetch_fini(tsk);
	memset(fetch, 0, sizeof(struct prefetching_state));
	tsk->obl.flags = flags;
	fetch->num_accesses = counts[id] / sizeof(void *);
	printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n",
	       counts[id], fetch->num_accesses);

	fetch->accesses = bufs[id];

	INIT_WORK(&fetch->prefetch_work, prefetch_work_func);
	fetch->prefetch_start = true; // can be used to pause and resume
}

void fetch_init(struct task_struct *tsk, int flags)
{
	int thread_ind = 0;
	do_swap_page_p = (void *)kallsyms_lookup_name("do_swap_page");
	atomic_set(&thread_pos, 0);
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

		bufs[thread_ind] = buf;
		counts[thread_ind] = count;
	}

	fetch_init_atomic(tsk, flags);
}

void fetch_clone(struct task_struct *p, unsigned long clone_flags)
{
	fetch_init_atomic(p, current->obl.flags);
}

void fetch_fini(struct task_struct *tsk)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	if (fetch->accesses != NULL) {
		cancel_work_sync(&fetch->prefetch_work);
		printk(KERN_INFO "fetch id %d: found %d/%d "
				 "min:%ld, maj: %ld "
				 "alrdy prsnt: %d %s\n",
		       current->pid - current->group_leader->pid,
		       fetch->found_counter, fetch->num_fault, current->min_flt,
		       current->maj_flt, fetch->already_present,
		       fetch->num_accesses - fetch->next_fetch < 10 ?
			       "SYNC" :
			       "!!!LOST!!!");
		printk(KERN_INFO "pos %ld next %ld num %ld\n", fetch->pos,
		       fetch->next_fetch, fetch->num_accesses);
		vfree(fetch->accesses);
		fetch->accesses = NULL; // todo:: should not need once in kernel
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
void fetch_page_fault_handler(struct pt_regs *regs, unsigned long error_code,
			      unsigned long address, struct task_struct *tsk,
			      bool *return_early, int magic)
{
	struct prefetching_state *fetch = &tsk->obl.fetch;
	int dist = 0;

	if (unlikely(fetch->accesses == NULL)) {
		printk(KERN_ERR "fetch trace not initialized\n");
		return;
	}

	if (unlikely(error_code & PF_INSTR)) {
		//printk(KERN_ERR "instr fault %lx", address);
		return;
	}
	fetch->num_fault++;
	/*

	// aggressively stay in sync with tape
	while (dist < MAX_SEARCH_DIST &&
	       fetch->pos + dist + 1 < fetch->num_accesses &&
	       (fetch->accesses[fetch->pos + dist] & PAGE_ADDR_MASK) !=
		       (address & PAGE_ADDR_MASK))
		dist++;
	if ((fetch->accesses[fetch->pos + dist] & PAGE_ADDR_MASK) ==
	    (address & PAGE_ADDR_MASK)) {
		fetch->pos += dist;
		fetch->counter++;
	}
*/

	/* first condition is relevant in the very beginning
	 * second condition uses the above syncing to follow the tape
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
		    (address & PAGE_ADDR_MASK)) {
		if (memtrace_getflag(OFFLOAD_FETCH))
			queue_work_on(FETCH_OFFLOAD_CPU, system_highpri_wq,
				      &fetch->prefetch_work);
		else
			prefetch_work_func(&tsk->obl.fetch.prefetch_work);
	}
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

	// todo:: old artifact from explicit eviction and pfn lifetime tracking experiments..
	// remove after sorting out evictions!!
	if (false && fetch_buf_end < fetch_print_buf + sizeof(fetch_print_buf))
		fetch_buf_end += snprintf(fetch_buf_end, 10, "%lx,",
					  (unsigned long)page_to_pfn(page));
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
