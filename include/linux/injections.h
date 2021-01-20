#ifndef INJECTIONS_H
#define INJECTIONS_H

typedef void (*injected_func_type)();
extern injected_func_type pointers[100];
extern void kernel_noop(void);
extern void set_pointer(int i, void (*f)());

enum memtrace_state_lags {
	TAPE_OPS = 1 << 0,
	SWAP_SSD_OPTIMIZATION = 1 << 1,
	FASTSWAP_ASYNCWRITES = 1 << 2,

	TAPE_FETCH = 1 << 3,
	PAGE_BUFFER_ADD = 1 << 4,
	PAGE_BUFFER_EVICT = 1 << 5,

	//FRONTSWAP_ON = 1 << 3,
};
// space for mem_pattern_trace state which needs to live accross injector
// module reloads.
typedef struct {
	int flags;
	// in case I need to add more fields before doing a kernel recompile
	char padding[60];
} mem_pattern_trace_state;

extern mem_pattern_trace_state memtrace_state;

static inline void memtrace_setflag(int flag)
{
	memtrace_state.flags |= flag;
}

static inline int memtrace_getflag(int flag)
{
	return memtrace_state.flags & flag;
}

static inline void memtrace_clearflag(int flag)
{
	memtrace_state.flags &= ~flag;
}

#endif /* INJECTIONS_H */
