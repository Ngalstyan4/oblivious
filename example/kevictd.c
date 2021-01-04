#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/mm.h> //for page_mapped
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

typedef struct {
	struct task_struct *tsk;

	struct timespec ts;
	char refs[8001];

} kevictd_state;

static const int TTL = 30;
static kevictd_state kevictd;
static int kevictd_thread(void *data)
{
	int i = 0, j = 0;
	int cnt = 0;
	bool init = false;
	while (!kthread_should_stop()) {
		struct timespec ts;
		getnstimeofday(&ts);
		int mcount = 0;
		if (ts.tv_sec - kevictd.ts.tv_sec > TTL) {
			kevictd.tsk = NULL;
			printk(KERN_ERR "kevictd: deamon timed out\n");
			break;
		}
		if (!fetch.prefetch_start)
		{
			msleep(14000);
			continue;
		}
		memset(kevictd.refs, 0, 8001);
		for (i = 0, mcount = 0; i < get_buffer_size(); i++) {
			struct page *p = prefetch_buffer.page_data[i];
			// try page_mapped, PageReferenced (PageWriteback(p) || PageDirty(p))
			// , PageActive
			if (p != NULL && PageSwapBacked(p)) {
				kevictd.refs[i] = 'M';
				mcount++;
			} else {
				if (p == NULL)
					kevictd.refs[i] = '0';
				else

					kevictd.refs[i] = '.';
			}
		}
		if (!init && fetch.mm) {

			down_read(&fetch.mm->mmap_sem);
			for (i = 19000, cnt = 0; i < 19185; i++) {
				if (fetch.mm && fetch.prefetch_start &&
				    prefetch_addr(fetch.accesses[i],
						  fetch.mm) == true)
					cnt++;
			}
			up_read(&fetch.mm->mmap_sem);

			if (cnt > 15) {
				init = true;
			}
			printk(KERN_INFO "prefetchted init %d pages", cnt);
		}
		//if (mcount > 0)
		printk(KERN_INFO "kevictd: %d buff %s\n", get_buffer_size(),
		       kevictd.refs);
	}
	return 0;
}

void kevictd_init()
{
	memset(&kevictd, 0, sizeof(kevictd));
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
		if (IS_ERR_VALUE(err)) {
			printk(KERN_ERR "kevictd: unable to stop kevictd "
					"system daemon, err: %d\n",
			       err);
		} else {
			printk(KERN_INFO "kevictd: stopped the daemon\n");
		}
		kevictd.tsk = NULL;
	}
}

