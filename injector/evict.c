#include <linux/workqueue.h>
#include <linux/memcontrol.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/swap.h>

#include <linux/injections.h>

typedef struct {
	unsigned long high_work_call_cnt;
	unsigned long reclaim_cnt;
	unsigned long nr_reclaimed;
} evict_state;

static evict_state evict;
unsigned long do_try_to_free_pages(struct zonelist *zonelist,
				   struct scan_control *sc);

void reclaim_high(struct mem_cgroup *memcg, unsigned int nr_pages,
		  gfp_t gfp_mask);

static void high_work_func_30(struct work_struct *work,
			      struct mem_cgroup *memcg, unsigned long high,
			      unsigned long nr_pages, bool *skip)
{
	int frac = 7;
	*skip = true;
	evict.high_work_call_cnt++;

	if (nr_pages > high * frac / 10) {
		unsigned long reclaim = nr_pages - high * frac / 10;

		evict.reclaim_cnt++;
		// reclaim high has a check and does not reclaim beyond
		// memory limit that's why we call try_to_free directly
		//reclaim_high(memcg, reclaim, GFP_KERNEL);
		evict.nr_reclaimed += try_to_free_mem_cgroup_pages(
			memcg, reclaim, GFP_KERNEL, true);
	}

	if (nr_pages > high * frac / 10)
		schedule_work_on(7, &memcg->high_work);
}

/*static void swap_writepage_32(struct page *page, struct writeback_control *wbc,
			      bool *skip) {} */

void evict_init()
{
	printk(KERN_INFO "init evict injections\n");
	memset(&evict, 0, sizeof(evict));
	set_pointer(30, high_work_func_30);
	//set_pointer(32, swap_writepage_32);
}

void evict_fini()
{
	printk(KERN_INFO "Eviction stats:\n"
			 "num high_work calls: %ld\n"
			 "num reclaims: %ld\n"
			 "pages reclaimed: %ld\n",
	       evict.high_work_call_cnt, evict.reclaim_cnt, evict.nr_reclaimed);
	set_pointer(30, kernel_noop);
	set_pointer(32, kernel_noop);
}
