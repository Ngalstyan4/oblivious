#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/socket.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hasan Al Maruf");
MODULE_DESCRIPTION("Kernel module to enable/disable Leap components");
extern void kernel_noop(void);
char *cmd;
unsigned long tried = 0;
char *process_name;
pid_t process_pid = 0;
MODULE_PARM_DESC(cmd, "A string, for prefetch load/unload command");
module_param(cmd, charp, 0000);
MODULE_PARM_DESC(process_name, "A string, for process name");
module_param(process_name, charp, 0000);

void haha(void)
{
	printk(KERN_INFO "injected print statement- C Narek Galstyan");
}
EXPORT_SYMBOL(haha);

void find_trend_1(void)
{
	printk(KERN_INFO "in find trend");
}
EXPORT_SYMBOL(find_trend_1);

/*
 * Page fault error code bits:
 *
 *   bit 0 ==	 0: no page found	1: protection fault
 *   bit 1 ==	 0: read access		1: write access
 *   bit 2 ==	 0: kernel-mode access	1: user-mode access
 *   bit 3 ==				1: use of reserved bit detected
 *   bit 4 ==				1: fault was an instruction fetch
 */
enum x86_pf_error_code {

	PF_PROT = 1 << 0,
	PF_WRITE = 1 << 1,
	PF_USER = 1 << 2,
	PF_RSVD = 1 << 3,
	PF_INSTR = 1 << 4,
};

int i = 0;
void do_page_fault_2(unsigned long error_code, unsigned long address,
		     struct task_struct *tsk)
{
	if (process_pid == tsk->pid && i++ < 10000) {
		printk(KERN_INFO "%dth time in do page fault [%s | %s | %s | "
				 "%s | %s]  %lx %d",
		       i, error_code & PF_PROT ? "PROT" : "",
		       error_code & PF_WRITE ? "WRITE" : "READ",
		       error_code & PF_USER ? "USER" : "KERNEL",
		       error_code & PF_RSVD ? "SPEC" : "",
		       error_code & PF_INSTR ? "INSTR" : "", address, tsk->pid);
	}
}
EXPORT_SYMBOL(do_page_fault_2);

static int get_pid_for_process(void)
{
	int pid = -1;
	struct task_struct *task;
	for_each_process (task) {
		if (strcmp(process_name, task->comm) == 0) {
			pid = task->pid;
			printk(KERN_INFO "Process id of %s process is %i\n",
			       process_name, task->pid);
		}
	}
	return pid;
}

static int process_find_init(void)
{
	int pid = -1;
	printk(KERN_INFO "Initiating process find for %s!\n", process_name);
	if (!process_name) {
		printk(KERN_INFO "Invalid process_name\n");
		return -1;
	}
	while (pid == -1) {
		pid = get_pid_for_process();
		tried++;
		if (tried > 30)
			break;
		if (pid == -1)
			msleep(1000); // milisecond sleep
	}
	if (pid != -1) {
		process_pid = pid;
		set_process_id(pid);
		set_pointer(2, do_page_fault_2);
		printk("PROCESS ID set for remote I/O -> %ld\n",
		       get_process_id());
	} else {
		printk(KERN_INFO "Failed to track process within %ld seconds\n",
		       tried);
	}
	return 0;
}

static void usage(void)
{
	printk(KERN_INFO "To enable remote I/O data path: insmod "
			 "leap_functionality.ko process_name=\"tunkrank\" "
			 "cmd=\"init\"\n");
	printk(KERN_INFO "To disable remote I/O data path: insmod "
			 "leap_functionality.ko cmd=\"fini\"\n");
	printk(KERN_INFO "To enable prefetching: insmod leap_functionality.ko "
			 "cmd=\"prefetch\"\n");
	printk(KERN_INFO "To disable prefetching: insmod leap_functionality.ko "
			 "cmd=\"readahead\"\n");
	printk(KERN_INFO "To have swap info log: insmod leap_functionality.ko "
			 "cmd=\"log\"\n");
}

static int __init leap_functionality_init(void)
{
	set_pointer(0, haha);
	set_pointer(1, find_trend_1);
	// set_pointer(2, do_page_fault_2); // <-- set up in  proc attach init
	if (!cmd) {
		usage();
		return 0;
	}
	if (strcmp(cmd, "init") == 0) {
		process_find_init();
		swap_info_log();
		return 0;
	}
	if (strcmp(cmd, "fini") == 0) {
		set_process_id(0);
		return 0;
	}
	if (strcmp(cmd, "prefetch") == 0) {
		init_swap_trend(32);
		set_custom_prefetch(1);
		return 0;
	} else if (strcmp(cmd, "log") == 0) {
		swap_info_log();
		return 0;
	} else if (strcmp(cmd, "readahead") == 0) {
		set_custom_prefetch(0);
		return 0;
	} else
		usage();
	return 0;
}

static void __exit leap_functionality_exit(void)
{
	int i;
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);
	printk(KERN_INFO "Cleaning up leap functionality sample module.\n");
}

module_init(leap_functionality_init);
module_exit(leap_functionality_exit);
