#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/swap.h> //todo:: q:: reqired for swapops import because of SWP_MIGRATION_READ. is it ok?
#include <linux/swapops.h> // for tape prefetching injection
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/socket.h>

#include <linux/kthread.h>

typedef struct {
	struct task_struct *tsk;

} kevictd_state;

static kevictd_state kevictd;

static int kevictd_thread(void *data)
{

	printk(KERN_INFO "kevictd: in evict daemon");
	return 0;
}

void kevictd_init()
{
	memset(&kevictd, 0, sizeof(kevictd));
	kevictd.tsk = kthread_create_on_cpu(kevictd_thread, NULL, 4,
					    "kevictd-daemon");
	if (IS_ERR(kevictd.tsk)) {
		printk("unable to launch kevictd system daemon, err: %ld",
		       PTR_ERR(kevictd.tsk));
		kevictd.tsk = NULL;
	}
}
void kevictd_fini()
{
	if (kevictd.tsk) {
		int err = kthread_stop(kevictd.tsk);
		if (IS_ERR_VALUE(err)) {
			printk("unable to stop kevictd system daemon, err: %d",
			       err);
		}
		kevictd.tsk = NULL;
	}
}
