#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/mm.h> //for page_mapped
#include <linux/kthread.h>

struct pref_buffer {
	atomic_t head;
	atomic_t tail;
	atomic_t size;
	swp_entry_t *offset_list;
	struct page **page_data;
	spinlock_t buffer_lock;
};
extern struct pref_buffer prefetch_buffer;
extern int get_buffer_tail(void);
extern int get_buffer_size(void);
extern void inc_buffer_head(void);
extern void inc_buffer_tail(void);
extern void inc_buffer_size(void);
extern void dec_buffer_size(void);
extern int is_buffer_full(void);

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
	while (!kthread_should_stop()) {
		struct timespec ts;
		getnstimeofday(&ts);
		if (ts.tv_sec - kevictd.ts.tv_sec > TTL) {
			kevictd.tsk = NULL;
			printk(KERN_ERR "kevictd: deamon timed out\n");
			break;
		}
		if (get_buffer_size() >= 8000) msleep(100);

		memset(kevictd.refs, 0, 8001);
		for (i = 0; i < get_buffer_size(); i++) {
			struct page *p = prefetch_buffer.page_data[i];
			// try page_mapped, PageReferenced (PageWriteback(p) || PageDirty(p))
			// , PageActive
			if (p != NULL && page_mapped(p)) {
				kevictd.refs[i] = 'M';
			} else {
				kevictd.refs[i] = '.';
			}
		}
		printk(KERN_INFO "kevictd: %d buff %s", get_buffer_size(), kevictd.refs);
		msleep(10);
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

