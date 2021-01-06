#include <linux/delay.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/pagemap.h> // remove, needed for find_get_page

#include "utils.h"
#include "ring_buffer.h"
#include "page_buffer.h"

#define PREFETCH_BUFFER_SIZE 128
struct page* prefetch_pages[PREFETCH_BUFFER_SIZE];
int current_ind = 0;

// if performance becomes a bottleneck, switch to a wait free design `
// https://www.kernel.org/doc/html/v5.9-rc4/trace/ring-buffer-design.html
void my_add_page_to_buffer(struct page *page)
{
	prefetch_pages[current_ind++] = page;
	current_ind = current_ind % PREFETCH_BUFFER_SIZE;
}

void my_add_page_to_buffer_delay(struct page *page)
{
	//	msleep(1);
}

bool prefetch_addr(unsigned long addr, struct mm_struct *mm)
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

	// todo:: maybe use ****** find_vma(mm, addr) *** instead of using the first vma?
	//
	// todo:: kernel doc Documentation/vm/unevictable-lru.txt talks about interaction of cgroups with
	// pages that are mlocked or otherwise marked as unevictable. It also talks about page_evictable()
	// as well as mechanisms to mark a *whole address space* as unevictable. perhaps we can use this
	// mechanism to prefetch into a separate address space that is not evictable to make sure 
	// 1) the kernel does not meddle with prefetching
	// 2) we do not stall the kernel trying to evict things from our prefetched list that we are sure it is not 
	// going to succeed.
//	page = read_swap_cache_async(rmem_entry, GFP_HIGHUSER_MOVABLE, mm->mmap,
//				     addr);

	allocated = 44;
	page = __read_swap_cache_async(rmem_entry, GFP_HIGHUSER_MOVABLE, mm->mmap,
				     addr, &allocated);
	if (!page)
		return false;

	if (!allocated)
	{
		put_page(page); //= page_cache_release
		return false;
	}
	swap_readpage(page);
	SetPageReadahead(page);
	//Q:: todo:: do we need this in tape prefetching since we assume swap space is unlimitted?
	put_page(page); //= page_cache_release
	my_add_page_to_buffer(page);
	return true;
}
