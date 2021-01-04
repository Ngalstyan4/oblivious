#include <linux/delay.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include "utils.h"
#include "ring_buffer.h"
#include "page_buffer.h"

// if performance becomes a bottleneck, switch to a wait free design `
// https://www.kernel.org/doc/html/v5.9-rc4/trace/ring-buffer-design.html
void my_add_page_to_buffer(struct page *page)
{
	int tail;
	// swp_entry_t head_entry;
	// struct page *head_page;

	if (is_buffer_full())
		return;
	spin_lock_irq(&prefetch_buffer.buffer_lock);
	inc_buffer_tail();
	tail = get_buffer_tail();
	prefetch_buffer.page_data[tail] = page;
	inc_buffer_size();
	spin_unlock_irq(&prefetch_buffer.buffer_lock);
	msleep(1);
}
void my_add_page_to_buffer_delay(struct page *page)
{
	//	msleep(1);
}

bool prefetch_addr(unsigned long addr, struct mm_struct *mm)
{
	struct page *page;
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
	//
	// todo:: maybe use ****** find_vma(mm, addr) *** instead of using the first vma?
	page = read_swap_cache_async(rmem_entry, GFP_HIGHUSER_MOVABLE, mm->mmap,
				     addr);
	if (!page)
		return false;
	//if (PageReadahead(page)) return false;
	SetPageReadahead(page);
	//Q:: todo:: do we need this in tape prefetching since we assume swap space is unlimitted?
	put_page(page); //= page_cache_release
	my_add_page_to_buffer(page);
	return true;
}
