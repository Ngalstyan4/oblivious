
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/injections.h>

#include "common.h"
#include "fetch.h"
#include "kevictd.h"

#define MAX_PRINT_LEN 768

// exported from mm/memory.c mm/internal.h function
int do_swap_page(struct vm_fault *vmf);
//todo::
// dangerous!! probably need to protect by locks/atomics? but seems to work for now..
char fetch_print_buf[MAX_PRINT_LEN];
char *fetch_buf_end = fetch_print_buf;
typedef struct {
	pid_t process_pid;
	int counter;
	int found_counter;
	int already_present;
	int num_fault;
	struct mm_struct *mm;
	struct work_struct prefetch_work;
	unsigned long *accesses;
	unsigned long num_accesses;
	unsigned long pos;
	unsigned long next_fetch;
	// controlls whether swapin_readahead will use tape to prefetch or not
	bool prefetch_start;
} prefetching_state;

//todo:: switch back to static
prefetching_state fetch;

static bool prefetch_addr(unsigned long addr, struct mm_struct *mm,
			  struct vm_fault *vmf);
static void do_page_fault_fetch_2(struct pt_regs *regs,
				  unsigned long error_code,
				  unsigned long address,
				  struct task_struct *tsk, bool *return_early,
				  int magic);

static void prefetch_work_func(struct work_struct *work)
{
	int i = 0;
	int num_prefetch = 0;
	struct vm_fault vmf;
	unsigned long paddr;
	pte_t *pte;
	//q:: what is down_read? is it not necessary here?
	//down_read(&fetch.mm->mmap_sem);
	unsigned long fetch_start = fetch.next_fetch;
	if (unlikely(!memtrace_getflag(TAPE_FETCH)))
		return;
	for (i = 0; i < 100 && num_prefetch < 50; i++) {
		if (unlikely(fetch_start + i >= fetch.num_accesses)) {
			i--;
			break;
		}
		paddr = fetch.accesses[fetch_start + i];

		if (prefetch_addr(paddr, fetch.mm, &vmf) == true) {
			num_prefetch++;
			//do_swap_page(&vmf);
		}
	}
	fetch.found_counter++;
	//lru_add_drain();// <Q::todo:: what does this do?.. Push any new pages onto the LRU now
	fetch.next_fetch = fetch_start + i;
	while (true) {
		if (unlikely(fetch.next_fetch >= fetch.num_accesses)) {
			fetch.next_fetch = fetch.num_accesses - 1;
			break;
		}
		pte = addr2pte(fetch.accesses[fetch.next_fetch], fetch.mm);
		if (pte && !pte_none(*pte) && pte_present(*pte)) {
			// page already mapped in page table
			fetch.next_fetch++;
			fetch.already_present++;
		} else
			break;
	}

	//up_read(&fetch.mm->mmap_sem);
}

void fetch_init(pid_t pid, const char *proc_name, struct mm_struct *mm)
{
	char trace_filepath[FILEPATH_LEN];
	size_t filesize = 0;
	unsigned long *buf;
	size_t count;

	snprintf(trace_filepath, FILEPATH_LEN, FETCH_FILE_FMT, proc_name);

	// in case path is too long, truncate;
	trace_filepath[FILEPATH_LEN - 1] = '\0';

	// if previous program running fetching was killed
	// before completion, fini() would heve never run
	// there must be a better way to handle this (listen to do_exit calls?)
	// but for now this works
	fetch_fini();
	memset(&fetch, 0, sizeof(fetch));

	if (!file_exists(trace_filepath)) {
		printk(KERN_ERR "unable to read fetch trace\n");
		return;
	}

	filesize = file_size(trace_filepath);

	buf = vmalloc(filesize);
	if (buf == NULL) {
		printk(KERN_ERR
		       "unable to allocate memory for reading the trace\n");
		return;
	}

	count = read_trace(trace_filepath, (char *)buf, filesize);

	if (filesize == 0 || count == 0) {
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

	INIT_WORK(&fetch.prefetch_work, prefetch_work_func);
	fetch.prefetch_start = true; // can be used to pause and resume
	set_pointer(2, do_page_fault_fetch_2);
}

void fetch_clone(struct task_struct *p, unsigned long clone_flags)
{
}

void fetch_fini()
{
	if (fetch.accesses != NULL) {
		cancel_work_sync(&fetch.prefetch_work);
		fetch.mm = NULL;
		printk(KERN_INFO "found %d/%d page faults: min:%ld, maj: %ld "
				 "already present: %d next_fetch: %ld\n",
		       fetch.found_counter, fetch.num_fault, current->min_flt,
		       current->maj_flt, fetch.already_present,
		       fetch.next_fetch);
		vfree(fetch.accesses);
		fetch.accesses = NULL; // todo:: should not need once in kernel
	}
	set_pointer(2, kernel_noop);
}

void fetch_force_clean()
{

	if (fetch.accesses) {
		vfree(fetch.accesses);
		fetch.accesses = NULL;
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
		fetch.num_fault++;

		if (fetch.next_fetch == 0 ||
		    (fetch.accesses[fetch.next_fetch] & PAGE_ADDR_MASK) ==
			    (address & PAGE_ADDR_MASK)) {
			prefetch_work_func(NULL);
		}
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

	if (fetch_buf_end < fetch_print_buf + sizeof(fetch_print_buf))
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
