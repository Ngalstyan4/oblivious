#ifndef TRACING_H
#define TRACING_H

enum trace_flags {
	TRACE_START = 1 << 0,
	TRACE_PAUSE = 1 << 1,
	TRACE_RESUME = 1 << 2,
	TRACE_END = 1 << 3,

	TRACE_RECORD = 1 << 4,
	TRACE_PREFETCH = 1 << 5,
	TRACE_AUTO = 1 << 6,
	// todo:: remove these, here for easy cli use only
	KEVICTD_INIT = 1 << 7,
	KEVICTD_FINI = 1 << 8,

	TRACE_MISC = 1 << 9,
	OBLIVIOUS_TAG = 1 << 10,
};

#define mem_pattern_trace 333

#endif /*TRACING_H*/
