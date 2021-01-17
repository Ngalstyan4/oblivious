#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/injections.h>

void kernel_noop(void)
{
}
injected_func_type pointers[100] = {[0 ... 99] = kernel_noop };

void set_pointer(int i, injected_func_type f)
{
	pointers[i] = f;
}
EXPORT_SYMBOL(set_pointer);
EXPORT_SYMBOL(kernel_noop);


mem_pattern_trace_state memtrace_state;
EXPORT_SYMBOL(memtrace_state);

