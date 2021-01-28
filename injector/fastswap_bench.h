#ifndef FASTSWAP_BENCH_H
#define FASTSWAP_BENCH_H

#include <linux/vmalloc.h>
#include <linux/frontswap.h>
#include <linux/pagemap.h>
#include <linux/time.h>
#include <linux/delay.h>

static inline void fastswap_bench()
{
	int i = 0, iter = 0;
	const long NUM_PAGES = 40000;
	const int NUM_ITERS = 10;
	char *buf = vmalloc(4096 * NUM_PAGES);
	struct page **pages = vmalloc(NUM_PAGES * sizeof(struct page *));
	char *p = NULL;
	ktime_t start;
	unsigned long duration;
	pte_t *pte = NULL;
	struct page *mid_page;
	for (p = buf; p < buf + 4096 * NUM_PAGES; p += 4096)
		*p = 44;
	printk(KERN_INFO "Start fastswap write throughput benchmark\n");
	if (buf == NULL || pages == NULL) {
		printk(KERN_ERR "unable to allocate buffer\n");
		return;
	}

	p = buf;
	for (i = 0; i < NUM_PAGES; i++, p += 4096) {
		pte = addr2pte((unsigned long)p, current->mm);
		mid_page = pte_page(*pte);
		pages[i] = mid_page;
	}

	for (iter = 0; iter < NUM_ITERS; iter++) {
		start = ktime_get();
		p = buf;
		for (i = 0; i < NUM_PAGES; i++, p += 4096) {
			mid_page = pages[i];
			if (memtrace_getflag(FASTSWAP_ASYNCWRITES)) {
				if (trylock_page(mid_page) &&
				    __frontswap_store_async(mid_page) == 0) {
					set_page_writeback(mid_page);
					unlock_page(mid_page);
					end_page_writeback(mid_page);
				}
			} else {
				if (trylock_page(mid_page) &&
				    __frontswap_store(mid_page) == 0) {
					set_page_writeback(mid_page);
					unlock_page(mid_page);
					end_page_writeback(mid_page);
				}
			}

			duration = ktime_to_us(ktime_sub(ktime_get(), start));
			if (duration > 2 * 1000 * 1000) {
				goto out;
			}
		}

		pte = addr2pte((unsigned long)buf, current->mm);
		mid_page = pte_page(*pte);
		if (trylock_page(mid_page) &&
		    __frontswap_store(mid_page) == 0) {
			set_page_writeback(mid_page);
			unlock_page(mid_page);
			set_page_writeback(mid_page);
		}

		duration = ktime_to_us(ktime_sub(ktime_get(), start));
	out:
		if (duration == 0)
			printk(KERN_INFO "Fastswap bench: duration too short, "
					 "increase NUM_PAGES\n");
		else
			printk(KERN_INFO "Fastswap bench: %ld pages evicted in "
					 "%ldus\trate: %ld MB/s\n",
			       NUM_PAGES, duration,
			       1000 * 1000 * PAGE_SIZE * NUM_PAGES / (1 << 20) /
				       duration);
	}

	/*********************  BENCHMARK ASYNC READS ********************************/
	// todo:: if reads become a bottleneck, revisit this and export a drain_queue()
	// function from fastswap to complete all pending reads and benchmark this properly
	/*
	for (iter = 0; iter < NUM_ITERS; iter++) {
		start = ktime_get();
		p = buf;
		for (i = 0; i < NUM_PAGES; i++, p += 4096) {
			pte = addr2pte((unsigned long)p, current->mm);
			mid_page = pte_page(*pte);
			if (trylock_page(mid_page) &&
			    __frontswap_load_async(mid_page) == 0) {
			}
		}

		pte = addr2pte((unsigned long)buf, current->mm);
		mid_page = pte_page(*pte);
		if (trylock_page(mid_page) && __frontswap_load(mid_page) == 0) {
		}

		duration = ktime_to_us(ktime_sub(ktime_get(), start));
		if (duration == 0)
			printk(KERN_INFO "Fastswap bench: duration too short, "
					 "increase NUM_PAGES\n");
		else
			printk(KERN_INFO "Fastswap bench: %ld pages read in "
					 "%ldns\trate: %ld MB/s\n",
			       NUM_PAGES, duration,
			       1000 * 1000 * PAGE_SIZE * NUM_PAGES / (1 << 20) /
				       duration);
	}
	*/
	msleep(500);
	vfree(buf);
}
#endif /*FASTSWAP_BENCH_H*/
