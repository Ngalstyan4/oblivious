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
	/* Statistics on how prefetching is going. */
	int counter;
	int found_counter;
	int already_present;
	int num_fault;

	/* Used to schedule prefetching work asynchronously. */
	struct work_struct prefetch_work;

	/* The tape, buffered in memory. */
	unsigned long *tape;

	/* The length of the tape. */
	unsigned long tape_length;

	/* Index of the key page. */
	unsigned long key_page_idx;

	/*
	* Index of the next page to prefetch, without changes from other threads
	* (used only for debugging).
	*/
	unsigned long prefetch_next_idx;
};

struct task_struct_oblivious {
	// thread index
	int tind;
	int flags;
	struct trace_recording_state record;
	struct prefetching_state fetch;
};

#endif /* TASK_STRUCT_OBLIVIOUS_H */
