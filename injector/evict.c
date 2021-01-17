#include <linux/workqueue.h>
#include <linux/memcontrol.h>
#include <linux/delay.h>
#include <linux/printk.h>

#include <linux/injections.h>

void reclaim_high(struct mem_cgroup *memcg, unsigned int nr_pages,
		  gfp_t gfp_mask);
static void high_work_func_30(struct work_struct *work,
			      struct mem_cgroup *memcg, unsigned long high,
			      unsigned long nr_pages, bool *skip)
{
	*skip = true;
	//	if (nr_pages > high)
	//		printk(KERN_INFO "evict: nr_pages: %lu high: %lu %lu\n", nr_pages, high, high * 7/10);

	if (nr_pages > high * 7 / 10 || true) {
		unsigned long reclaim = nr_pages - high * 7 / 10;

		/* reclaim_high only reclaims iff nr_pages > high */
		reclaim_high(memcg, reclaim, GFP_KERNEL);
	}
	if (nr_pages > high * 7 / 10)
		schedule_work_on(7, &memcg->high_work);
}

static void swap_writepage_32(struct page *page, struct writeback_control *wbc,
			      bool *skip)
{
}

void evict_init()
{
	printk(KERN_INFO "init evict injections\n");
	set_pointer(30, high_work_func_30);
	//set_pointer(32, swap_writepage_32);
}

void evict_fini()
{
	set_pointer(30, kernel_noop);
	set_pointer(32, kernel_noop);
}
