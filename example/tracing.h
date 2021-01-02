#ifndef TRACING_H
#define TRACING_H

enum trace_flags {
	START = 1 << 0,
	PAUSE = 1 << 1,
	RESUME = 1 << 2,
	END = 1 << 3,

	RECORD = 1 << 4,
	PREFETCH = 1 << 5,
	AUTO = 1 << 6,

	KEVICTD_INIT = 1<<7,
	KEVICTD_FINI = 1<<8,
};

#define mem_pattern_trace 328

#endif /*TRACING_H*/
