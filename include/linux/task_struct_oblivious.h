#ifndef TASK_STRUCT_OBLIVIOUS_H
#define TASK_STRUCT_OBLIVIOUS_H
#include <linux/workqueue.h>
#include <linux/ktime.h> // Needed for stats
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

#define OBL_MAX_NUM_THREADS 20

/* Based on https://github.com/ucbrise/mage/blob/main/src/util/stats.hpp */
struct stream_stats {
	u64 stat_max;
	u64 stat_sum;
	u64 stat_min;
	u64 stat_count;
};

static inline void stats_event(struct stream_stats *s, u64 stat) {
	if (unlikely(s->stat_count == 0)) {
		s->stat_max = stat;
		s->stat_sum = stat;
		s->stat_min = stat;
		s->stat_count = 1;
	} else {
		if (unlikely(stat > s->stat_max)) {
			s->stat_max = stat;
		}
		s->stat_sum += stat;
		if (unlikely(stat < s->stat_min)) {
			s->stat_min = stat;
		}
		s->stat_count++;
	}
}

static inline void stats_tell(struct stream_stats *s, const char *label) {
	printk("%s: ( min = %llu, avg = %llu, max = %llu, count = %llu, sum = %llu )",
	    label, (unsigned long long) s->stat_min,
	    (unsigned long long) (s->stat_count == 0 ? 0 : (s->stat_sum / s->stat_count)),
	    (unsigned long long) s->stat_max, (unsigned long long) s->stat_count,
	    (unsigned long long) s->stat_sum);
}

struct trace_recording_state {
	unsigned long *accesses;
	unsigned long pos;
	struct file *f;

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

	/* Measure statistics for the work done on each batch. */
	struct stream_stats timing_stats;
	struct stream_stats batch_stats;
	struct stream_stats bandwidth_stats;
	u64 last_key_page_total_fetched;
	u64 last_key_page_num_fetched;
	u64 last_key_page_time;

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

struct process_state {
	spinlock_t key_page_indices_lock;
	atomic_t num_threads;
	unsigned long long int key_page_indices[OBL_MAX_NUM_THREADS];
	atomic_long_t map_intent[OBL_MAX_NUM_THREADS];
	unsigned long *bufs[OBL_MAX_NUM_THREADS];
	size_t counts[OBL_MAX_NUM_THREADS];
};

struct task_struct_oblivious {
	// thread index
	int tind;
	int flags;
	struct trace_recording_state record;
	struct prefetching_state fetch;
	struct process_state *proc;
};

#endif /* TASK_STRUCT_OBLIVIOUS_H */
