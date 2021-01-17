#include <linux/workqueue.h>
#include <linux/memcontrol.h>
#include <linux/delay.h>
#include <linux/printk.h>

#include <linux/pagemap.h>
#include <linux/frontswap.h>

#include <linux/mm.h> //for page_mapped
#include <linux/pagemap.h> // for unlock_page
#include <linux/fs.h> // for struct adress_space
#include <linux/swap.h> // for swapper_spaces
#include <linux/swapops.h> //for swp_type
#include <linux/rmap.h> // try_to_unmap

#include <linux/buffer_head.h> // for pageout->try_to_free buffers
#include <trace/events/vmscan.h>
#include <linux/backing-dev.h>

#include <linux/injections.h>

int my_swap_writepage_sync(struct page *page, struct writeback_control *wbc)
{
	int ret = 0;

	if (__frontswap_store(page) == 0) {
		set_page_writeback(page);
		unlock_page(page);
		end_page_writeback(page);
		goto out;
	}
out:
	return ret;
}

struct scan_control {
	/* How many pages shrink_list() should reclaim */
	unsigned long nr_to_reclaim;

	/* This context's GFP mask */
	gfp_t gfp_mask;

	/* Allocation order */
	int order;

	/*
	 * Nodemask of nodes allowed by the caller. If NULL, all nodes
	 * are scanned.
	 */
	nodemask_t	*nodemask;

	/*
	 * The memory cgroup that hit its limit and as a result is the
	 * primary target of this reclaim invocation.
	 */
	struct mem_cgroup *target_mem_cgroup;

	/* Scan (total_size >> priority) pages at once */
	int priority;

	/* The highest zone to isolate pages for reclaim from */
	enum zone_type reclaim_idx;

	/* Writepage batching in laptop mode; RECLAIM_WRITE */
	unsigned int may_writepage:1;

	/* Can mapped pages be reclaimed? */
	unsigned int may_unmap:1;

	/* Can pages be swapped as part of reclaim? */
	unsigned int may_swap:1;

	/* Can cgroups be reclaimed below their normal consumption range? */
	unsigned int may_thrash:1;

	unsigned int hibernation_mode:1;

	/* One of the zones is ready for compaction */
	unsigned int compaction_ready:1;

	/* Incremented by the number of inactive pages that were scanned */
	unsigned long nr_scanned;

	/* Number of pages freed so far during a call to shrink_zones() */
	unsigned long nr_reclaimed;
};

static int may_write_to_inode(struct inode *inode, struct scan_control *sc)
{
	if (current->flags & PF_SWAPWRITE)
		return 1;
	//GPL..cannot use for some reason.
	//todo:: make sure this path is not taken
	if (!inode_write_congested(inode))
		return 1;
	if (inode_to_bdi(inode) == current->backing_dev_info)
		return 1;
	return 0;
}

static void handle_write_error(struct address_space *mapping,
				struct page *page, int error)
{
	lock_page(page);
	if (page_mapping(page) == mapping)
		mapping_set_error(mapping, error);
	unlock_page(page);
}

static inline int is_page_cache_freeable(struct page *page)
{
	/*
	 * A freeable page cache page is referenced only by the caller
	 * that isolated the page, the page cache radix tree and
	 * optional buffer heads at page->private.
	 */
	return page_count(page) - page_has_private(page) == 2;
}

typedef enum {
	/* failed to write page out, page is locked */
	PAGE_KEEP,
	/* move page to the active list, page is locked */
	PAGE_ACTIVATE,
	/* page has been sent to the disk successfully, page is unlocked */
	PAGE_SUCCESS,
	/* page is clean and locked */
	PAGE_CLEAN,
} pageout_t;

pageout_t my_pageout(struct page *page, struct address_space *mapping,
			 struct scan_control *sc);
void my_pageout_33(struct page *page, struct address_space *mapping,
			struct scan_control *sc, bool *skip)
{
	*skip = my_pageout(page, mapping, sc);
}
/*
 * pageout is called by shrink_page_list() for each dirty page.
 * Calls ->writepage().
 */
pageout_t my_pageout(struct page *page, struct address_space *mapping,
			 struct scan_control *sc)

{
	/*
	 * If the page is dirty, only perform writeback if that write
	 * will be non-blocking.  To prevent this allocation from being
	 * stalled by pagecache activity.  But note that there may be
	 * stalls if we need to run get_block().  We could test
	 * PagePrivate for that.
	 *
	 * If this process is currently in __generic_file_write_iter() against
	 * this page's queue, we can perform writeback even if that
	 * will block.
	 *
	 * If the page is swapcache, write it back even if that would
	 * block, for some throttling. This happens by accident, because
	 * swap_backing_dev_info is bust: it doesn't reflect the
	 * congestion state of the swapdevs.  Easy to fix, if needed.
	 */
	if (!is_page_cache_freeable(page))
		return PAGE_KEEP;
	if (!mapping) {
		/*
		 * Some data journaling orphaned pages can have
		 * page->mapping == NULL while being dirty with clean buffers.
		 */
		if (page_has_private(page)) {
			if (try_to_free_buffers(page)) {
				ClearPageDirty(page);
				pr_info("%s: orphaned page\n", __func__);
				return PAGE_CLEAN;
			}
		}
		return PAGE_KEEP;
	}
	if (mapping->a_ops->writepage == NULL)
		return PAGE_ACTIVATE;
	if (!may_write_to_inode(mapping->host, sc))
		return PAGE_KEEP;

	if (clear_page_dirty_for_io(page)) {
		int res;
		struct writeback_control wbc = {
			.sync_mode = WB_SYNC_NONE,
			.nr_to_write = SWAP_CLUSTER_MAX,
			.range_start = 0,
			.range_end = LLONG_MAX,
			.for_reclaim = 1,
		};

		SetPageReclaim(page);
		res = mapping->a_ops->writepage(page, &wbc);
		if (res < 0)
			handle_write_error(mapping, page, res);
		if (res == AOP_WRITEPAGE_ACTIVATE) {
			ClearPageReclaim(page);
			return PAGE_ACTIVATE;
		}

		if (!PageWriteback(page)) {
			/* synchronous write or broken a_ops? */
			ClearPageReclaim(page);
		}
		//trace_mm_vmscan_writepage(page);
		inc_node_page_state(page, NR_VMSCAN_WRITE);
		return PAGE_SUCCESS;
	}

	return PAGE_CLEAN;
}

void reclaim_high(struct mem_cgroup *memcg,
			 unsigned int nr_pages,
			 gfp_t gfp_mask);
static void high_work_func_30(struct work_struct *work,
			      struct mem_cgroup *memcg, unsigned long high,
			      unsigned long nr_pages, bool *skip)
{
	*skip = true;
//	if (nr_pages > high)
//		printk(KERN_INFO "evict: nr_pages: %lu high: %lu %lu\n", nr_pages, high, high * 7/10);

	if (nr_pages > high*7/10 || true) {
		unsigned long reclaim = nr_pages - high*7/10;

		/* reclaim_high only reclaims iff nr_pages > high */
		reclaim_high(memcg, reclaim, GFP_KERNEL);
	}
	//if (nr_pages > high*7/10)
		schedule_work_on(7, &memcg->high_work);
}

static void no_skip_ssd_35(bool *skip_ssd) {
	*skip_ssd = false;
}

static void swap_writepage_32(struct page *page, struct writeback_control *wbc, bool *skip) {
	*skip = false;
}

void evict_init()
{
	printk(KERN_INFO "init evict injections\n");
	//set_pointer(30, high_work_func_30);
	set_pointer(32, swap_writepage_32);
	//set_pointer(33, my_pageout_33);
	//set_pointer(35, no_skip_ssd_35);
}

void evict_fini()
{
	set_pointer(30, kernel_noop);
	set_pointer(32, kernel_noop);
	set_pointer(33, kernel_noop);
	set_pointer(34, kernel_noop);
	//set_pointer(35, kernel_noop);
}
