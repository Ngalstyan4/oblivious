#ifndef TRACER_H
#define TRACER_H
#include <linux/linkage.h>
asmlinkage int sys_mem_pattern_trace(int flags);

#endif /*TRACER_H*/

