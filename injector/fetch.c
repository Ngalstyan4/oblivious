
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/injections.h>

#include "common.h"

const unsigned long MAX_SEARCH_DIST = 20;

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

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm);
static void do_page_fault_fetch_2(struct pt_regs *regs,
				  unsigned long error_code,
				  unsigned long address,
				  struct task_struct *tsk, bool *return_early,
				  int magic);

void fetch_init(pid_t pid, const char *proc_name, struct mm_struct *mm)
{
	char trace_filepath[FILEPATH_LEN];
	unsigned long *buf = vmalloc(TRACE_ARRAY_SIZE);
	size_t count;
	snprintf(trace_filepath, FILEPATH_LEN, TRACE_FILE_FMT, proc_name);

	// in case path is too long, truncate;
	trace_filepath[FILEPATH_LEN - 1] = '\0';

	if (buf == NULL) {
		printk(KERN_ERR
		       "unable to allocate memory for reading the trace\n");
		return;
	}

	memset(&fetch, 0, sizeof(fetch));

	count = read_trace(trace_filepath, (char *)buf, TRACE_ARRAY_SIZE);

	if (count == 0) {
		printk(KERN_ERR "unable to initialize fetching\n");
		vfree(buf);
		return;
	}

	fetch.num_accesses = count / sizeof(void *);
	printk(KERN_DEBUG "read %ld bytes which means %ld accesses\n", count,
	       fetch.num_accesses);

	fetch.accesses = buf;
	fetch.process_pid = pid;
	fetch.mm = mm;
	fetch.prefetch_start = true; // can be used to pause and resume
	set_pointer(2, do_page_fault_fetch_2);
}

void fetch_fini()
{
	if (fetch.accesses != NULL) {
		fetch.mm = NULL;
		printk(KERN_INFO "found %d/%d page faults: min:%ld, maj: %ld\n",
		       fetch.found_counter, fetch.counter, current->min_flt,
		       current->maj_flt);
		vfree(fetch.accesses);
		fetch.accesses = NULL; // todo:: should not need once in kernel
	}
}

void fetch_force_clean()
{

	if (fetch.accesses) {
		vfree(fetch.accesses);
		printk(KERN_INFO "found %d/%d page faults\n",
		       fetch.found_counter, fetch.counter);
	}
}

/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE BEGIN ++++++++++++++++++++*/
static void do_page_fault_fetch_2(struct pt_regs *regs,
				  unsigned long error_code,
				  unsigned long address,
				  struct task_struct *tsk, bool *return_early,
				  int magic)
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

		if (unlikely(fetch.pos >= fetch.next_fetch)) {
			down_read(&fetch.mm->mmap_sem);
			//debug_print_prefetch();
			for (i = 0; i < 500 && num_prefetch < 500; i++) {
				unsigned long paddr =
					fetch.accesses[fetch.pos + i];

				if (prefetch_addr(paddr, fetch.mm) == true)
					num_prefetch++;
			}
			//printk(KERN_INFO "num prefetch-%d, ii %d", num_prefetch, i);
			fetch.found_counter++;
			fetch.next_fetch = fetch.pos + i;
			// printk(KERN_INFO "num prefetch %d\n", num_prefetch);
			//lru_add_drain();
			up_read(&fetch.mm->mmap_sem);
		}
	}
}

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm)
{
	struct page *page;
	bool allocated = false;
	pte_t *pte;
	pte_t pte_val;
	swp_entry_t rmem_entry;
	pte = addr2pte(addr, mm);
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

	page = __read_swap_cache_async(rmem_entry, GFP_HIGHUSER_MOVABLE,
				       find_vma(mm, addr), addr, &allocated);
	if (!page)
		return false;

	if (!allocated) {
		put_page(page); //= page_cache_release
		return false;
	}
	swap_readpage(page);
	SetPageReadahead(page);
	put_page(page); //= page_cache_release
	//my_add_page_to_buffer(page);
	return true;
}
/* ++++++++++++++++++++++++++ PREFETCHING REMOTE MEMORY W/ TAPE END ++++++++++++++++++++++*/

