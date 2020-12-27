#include "tracer.h"
#include <linux/mm.h>
#include <linux/printk.h>

asmlinkage int sys_mem_pattern_trace_start(void) {
	printk (KERN_INFO "in mem pattern trace start");
	(*pointers[3])();
	return 0;
}
asmlinkage int sys_mem_pattern_trace_end(void) {
	printk (KERN_INFO "in mem pattern trace end");
	(*pointers[4])();
	return 0;
}
