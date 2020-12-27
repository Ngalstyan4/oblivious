#ifndef TRACER_H
#define TRACER_H
#include <linux/linkage.h>
asmlinkage int sys_mem_pattern_trace_start(void);
asmlinkage int sys_mem_pattern_trace_end(void);

#endif /*TRACER_H*/

