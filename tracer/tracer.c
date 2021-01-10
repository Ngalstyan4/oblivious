#include "tracer.h"
#include <linux/printk.h>
#include<linux/injections.h>

asmlinkage int sys_mem_pattern_trace(int flags) {
	printk (KERN_INFO "in mem pattern trace start %d", flags);
	(*pointers[3])(flags);
	return 0;
}

