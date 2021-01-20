#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/mm.h> //for page_mapped
#include <linux/pagemap.h> // for unlock_page
#include <linux/fs.h> // for struct adress_space
#include <linux/swap.h> // for swapper_spaces
#include <linux/swapops.h> //for swp_type
#include <linux/rmap.h> // try_to_unmap
#include <linux/kthread.h>

#include "ring_buffer.h"
#include "page_buffer.h"

typedef struct {
	int counter;
	int found_counter;
	struct mm_struct *mm;
	unsigned long *accesses;
	unsigned long num_accesses;
	unsigned long pos;
	// controlls whether swapin_readahead will use tape to prefetch or not
	bool prefetch_start;
} prefetching_state;
extern prefetching_state fetch;
void try_to_unmap_flush_dirty(void);
#define PREFETCH_BUFFER_SIZE 128
extern struct page *prefetch_pages[PREFETCH_BUFFER_SIZE];
extern int current_ind;
char dmesg_pointer[PREFETCH_BUFFER_SIZE + 1];

typedef struct {
	struct task_struct *tsk;

	struct timespec ts;
	char refs[8001];


	//counters
	unsigned long evict_try_cnt;
	unsigned long evict_cnt;

} kevictd_state;

static const int TTL = 300;
static kevictd_state kevictd;

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

pageout_t pageout(struct page *page, struct address_space *mapping,
		  struct scan_control *sc);
void try_to_unmap_flush(void);
int isolate_lru_page(struct page *page);
void putback_lru_page(struct page *page);
bool evict(struct page *p)
{
	struct address_space *mapping = NULL;

	if (!trylock_page(p))
		return false;

	if (PageWriteback(p))
		goto keep_locked;

	mapping = page_mapping(p);

	if (!mapping && PageAnon(p) && !PageSwapCache(p)) {
		if (PageReclaim(p)) printk (KERN_ERR "page reclaim %px\n", p);
		// add_to_swap needs the last arg for huge pages only
		//if (!add_to_swap(p, NULL))
			goto keep_locked;

		/* Adding to swap updated mapping */
		mapping = page_mapping(p);
	}

	if (page_mapped(p) && mapping) {
		switch (try_to_unmap(p, TTU_UNMAP /*| TTU_BATCH_FLUSH*/)) {
		case SWAP_FAIL:
			//printk(KERN_DEBUG "faailed to unmap page %px\n", p);
			goto keep_locked;
		case SWAP_AGAIN:
			printk(KERN_DEBUG
			       "faailed SWAP AGAIN to unmap page %px\n",
			       p);
			goto keep_locked;
		case SWAP_MLOCK:
			printk(KERN_DEBUG "faailed MLOCK to unmap page %px\n",
			       p);
			goto keep_locked;
		case SWAP_SUCCESS: /* try to write dirty page and evict it*/;
		}
	}

	if (!PageLRU(p)) goto keep_locked;
	isolate_lru_page(p);
//	printk(KERN_INFO "cache freeable %d %d", page_count(p),
//	       page_has_private(p));

	if (PageDirty(p)) {
		try_to_unmap_flush_dirty();
		switch (pageout(p, page_mapping(p), NULL)) {
		case PAGE_KEEP:
			//printk(KERN_DEBUG "evict %px PAGE_KEEP\n", p);
			goto keep_locked;
		case PAGE_ACTIVATE:
			printk(KERN_DEBUG "evict %px PAGE_ACTIVATE\n", p);
			goto keep_locked;
		case PAGE_SUCCESS:
			if (PageWriteback(p)) {
				goto keep;
			}
			if (PageDirty(p)) {
				printk(KERN_DEBUG
				       "evict %px PAGE_SUCCESS dirty\n",
				       p);
				goto keep;
			}
			goto keep;

		//	 if (!trylock_page(p))
		//		 goto keep;
		//	 if (PageDirty(p) || PageWriteback(p))
		//		 goto keep_locked;
		case PAGE_CLEAN:
			printk(KERN_DEBUG "evict %px PAGE_CLEAN\n", p);
			goto keep_locked;
		}
		printk(KERN_ERR "DONE WITH PAGOUT");
	}
keep_locked:
	try_to_unmap_flush();
	unlock_page(p);
	return false;
keep:
	try_to_unmap_flush();
	putback_lru_page(p);
	return true;
}

void debug_print_prefetch()
{
	int i;
	int mcount = 0;
	unsigned long addr;
	memset(kevictd.refs, 0, PREFETCH_BUFFER_SIZE * sizeof(char));
	for (i = 0, mcount = 0; i < PREFETCH_BUFFER_SIZE; i++) {
		struct page *p = prefetch_pages[i];
		// try page_mapped, PageReferenced (PageWriteback(p) || PageDirty(p))
		// , PageActive
		if (p != NULL && page_mapped(p)) {
			kevictd.refs[i] = PageReclaim(p) ?
						  'W' :
						  (PageDirty(p) ? 'D' : 'M');
			if (PageDirty(p))
				evict(p);
			mcount++;
		} else {
			if (p == NULL)
				kevictd.refs[i] = '0';
			else

				kevictd.refs[i] = '.';
		}
	}
	//printk(KERN_INFO "kevictd: %d buff %s\n", current_ind, kevictd.refs);
}

static int kevictd_thread(void *data)
{
	bool init = false;
	int i = 0;
	while (!kthread_should_stop()) {
		struct timespec ts;
		getnstimeofday(&ts);
		if (ts.tv_sec - kevictd.ts.tv_sec > TTL) {
			kevictd.tsk = NULL;
			printk(KERN_ERR "kevictd: deamon timed out\n");
			break;
		}
		if (!fetch.prefetch_start) {
			continue;
		}
		//for(i=0; i<10000000;i++)kevictd.evict_try_cnt++;
		// memset(kevictd.refs, 0, 8001);
		debug_print_prefetch();
		 // for(i=0; i < PREFETCH_BUFFER_SIZE; i++) {
		 // 	struct page *p = prefetch_pages[i];
		 // 	if ( p != NULL && page_mapped(p) && PageDirty(p)) {
		 // 		kevictd.evict_try_cnt++;
		 // 		if(evict(p)) kevictd.evict_cnt++;
		 // 	}
		 // }
		msleep(1);

	}
	return 0;
}

void kevictd_init()
{
	memset(&kevictd, 0, sizeof(kevictd));
	memset(&dmesg_pointer, ' ', sizeof(dmesg_pointer));
	dmesg_pointer[PREFETCH_BUFFER_SIZE] = '\0';
	getnstimeofday(&kevictd.ts);
	// todo:: maybe pin to a cpu with kthread_create_on_cpu later.
	// (symbol not exported as of now)
	kevictd.tsk = kthread_run(kevictd_thread, NULL, "kevictd-daemon");
	if (IS_ERR(kevictd.tsk)) {
		printk(KERN_ERR "kevictd: unable to launch kevictd system "
				"daemon, err: %ld\n",
		       PTR_ERR(kevictd.tsk));
		kevictd.tsk = NULL;
	}
}

void kevictd_fini()
{
	if (kevictd.tsk) {
		int err = kthread_stop(kevictd.tsk);
		printk(KERN_INFO"kevictd: evicted %ld/%ld tried \n", kevictd.evict_cnt, kevictd.evict_try_cnt);
		if (err < 0) {
			printk(KERN_ERR "kevictd: unable to stop kevictd "
					"system daemon, err: %d\n",
			       err);
		} else {
			printk(KERN_INFO "kevictd: stopped the daemon\n");
		}
		kevictd.tsk = NULL;
	}
}

