#ifndef TASK_STRUCT_OBLIVIOUS_H
#define TASK_STRUCT_OBLIVIOUS_H
#include <linux/workqueue.h>
/*
 * task_struct_oblivious is embedded in struct task_struct as ->obl
 * This ties oblivious-system state to each linux thread thereby allowing
 * trace recording/fetching of parallel applications as well as independent
 * fetching and recording of different applications
 * */

/*
 * N.B.: changing this file will result in a FULL KERNEL
 * rebuild (~18mins on 8 cores)
 * this file is included in sched.h as the structs are
 * embedded in task_struct.
 */
struct trace_recording_state {
	unsigned long *accesses;
	unsigned long pos;

	unsigned long microset_size;
	unsigned long microset_pos;
	unsigned long *microset;
};

struct prefetching_state {
	int counter;
	int found_counter;
	int already_present;
	int num_fault;
	struct work_struct prefetch_work;
	unsigned long *accesses;
	unsigned long num_accesses;
	unsigned long pos;
	unsigned long next_fetch;
	// controls whether swapin_readahead will use tape to prefetch or not
	bool prefetch_start;
};

struct task_struct_oblivious {
	// thread index
	int tind;
	int flags;
	struct trace_recording_state record;
	struct prefetching_state fetch;
};

#endif /* TASK_STRUCT_OBLIVIOUS_H */
