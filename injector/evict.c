#include <linux/workqueue.h>
#include <linux/memcontrol.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/swap.h>

#include <linux/mm_inline.h> // for lru_to_page and other inline lru stuff

#include "common.h"
#include <linux/injections.h>

#include <linux/frontswap.h>
#include <linux/pagemap.h>
#define MAX_PRINT_LEN 768
extern char fetch_print_buf[MAX_PRINT_LEN];
extern char *fetch_buf_end;
typedef struct {
	unsigned long high_work_call_cnt;
	unsigned long reclaim_cnt;
	unsigned long nr_reclaimed;
	char print_buf[MAX_PRINT_LEN];
} evict_state;

static evict_state evict;

void reclaim_high(struct mem_cgroup *memcg, unsigned int nr_pages,
		  gfp_t gfp_mask);

static void high_work_func_30(struct work_struct *work,
			      struct mem_cgroup *memcg, unsigned long high,
			      unsigned long nr_pages, bool *skip)
{
	int frac = 10;
	if (false) {
		//static int print_limit = 100;
		/*********************** UNDERSTANDING/PRINTING LRU CONTENT BEGIN *****************/
		struct zonelist *zonelist;
		struct zoneref *z;
		struct zone *zone;
		int nid;

		atomic_inc(&metronome);
		/*
		 * Unlike direct reclaim via alloc_pages(), memcg's reclaim doesn't
		 * take care of from where we get pages. So the node where we start the
		 * scan does not need to be the current node.
		 */
		nid = mem_cgroup_select_victim_node(memcg);

		zonelist = &NODE_DATA(nid)->node_zonelists[ZONELIST_FALLBACK];

		// nodemask=NULL below includes all nodes
		// reclaim_idx = MAX_NR_ZONES-1 indicates that pages can be isolated from all zones
		for_each_zone_zonelist_nodemask (zone, z, zonelist,
						 MAX_NR_ZONES - 1, NULL) {
			pg_data_t *pgdat = zone->zone_pgdat;
			struct lruvec *lruvec = mem_cgroup_lruvec(pgdat, memcg);
			enum lru_list lru;

			//get_scan_count(lruvec, memcg, sc, nr, num_lru_pages);
			for_each_evictable_anon_lru(lru)
			{
				// hm..struct pglist_data = pg_data_t ?but why??
				//struct pglist_data *pgdat =
				//	lruvec_pgdat(lruvec);
				struct list_head *src = &lruvec->lists[lru];
				struct page *page;
				char *buf_end;

				unsigned long lru_size =
					mem_cgroup_get_lru_size(lruvec, lru);
				if (list_empty(src))
					continue;

				memset(evict.print_buf, 0,
				       sizeof(evict.print_buf));
				buf_end = evict.print_buf;
				// Taking the lock is important and makes sure memory is not freed
				// under our feed resulting in a panic
				spin_lock_irq(zone_lru_lock(zone));
				// thre list list_for_each_entry_safe. not sure about all the diffs,
				// but I think it allows for modifying list elements as part of the
				// traversal.
				list_for_each_entry (page, src, lru) {
					if (buf_end >
					    evict.print_buf +
						    sizeof(evict.print_buf) -
						    10)
						break;
					buf_end += snprintf(
						buf_end, 10, "%lx,",
						(unsigned long)page_to_pfn(
							page));
				}

				spin_unlock_irq(zone_lru_lock(zone));
				printk(KERN_INFO "lru: %d %s %d:%s %ld %s", nid,
				       zone->name, atomic_read(&metronome),
				       lru == LRU_INACTIVE_ANON ? "inactive" :
								  "active",
				       lru_size, evict.print_buf);
			}

			if (fetch_buf_end != fetch_print_buf)
				printk(KERN_INFO "lru: %d:%s %s",
				       atomic_read(&metronome), "fetch",
				       fetch_print_buf);
			fetch_buf_end = fetch_print_buf;
		}
		msleep(1); //<-- add more delay for lower resolution, quicker recordings
		schedule_work_on(7, &memcg->high_work);
		/*********************** UNDERSTANDING/PRINTING LRU CONTENT END *****************/
		*skip = true;
		evict.high_work_call_cnt++;
		return;
	}
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

void evict_init()
{
	printk(KERN_INFO "init evict injections\n");
	memset(&evict, 0, sizeof(evict));
	set_pointer(30, high_work_func_30);

	atomic_set(&metronome, 0);
	debugfs_create_atomic_t("metronome", 0400, debugfs_root, &metronome);
}

void evict_fini()
{
	printk(KERN_INFO "Eviction stats:\n"
			 "num high_work calls: %ld\n"
			 "num reclaims: %ld\n"
			 "pages reclaimed: %ld\n",
	       evict.high_work_call_cnt, evict.reclaim_cnt, evict.nr_reclaimed);
	set_pointer(30, kernel_noop);
}
