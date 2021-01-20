#include <linux/delay.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection

#include <linux/pagemap.h> // remove, needed for find_get_page
#include <linux/rmap.h> // for try_t0_unmap

#include "common.h"
#include "ring_buffer.h"
#include "page_buffer.h"

#define PREFETCH_BUFFER_SIZE 128
struct page *prefetch_pages[PREFETCH_BUFFER_SIZE];
int current_ind = 0;

// if performance becomes a bottleneck, switch to a wait free design `
// https://www.kernel.org/doc/html/v5.9-rc4/trace/ring-buffer-design.html
void my_add_page_to_buffer(struct page *page)
{
	static int not_mapped = 0;
	static int mapped = 0;
	struct page *prev = prefetch_pages[current_ind];
	if (prev)
	{
		if (page_mapped(prev) && trylock_page(prev)){
			try_to_unmap(prev, TTU_UNMAP);
			unlock_page(page);
		}
	}
	prefetch_pages[current_ind++] = page;
	current_ind = current_ind % PREFETCH_BUFFER_SIZE;
}

void my_add_page_to_buffer_delay(struct page *page)
{
	//	msleep(1);
}

