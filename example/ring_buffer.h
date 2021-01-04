#ifndef RING_BUFFER_H
#define RING_BUFFER_H
// ring buffer impl thatshould be used only in page buffer
//todo:: if performance becomes critical, turn into a wait-free impl

struct pref_buffer {
	atomic_t head;
	atomic_t tail;
	atomic_t size;
	swp_entry_t *offset_list;
	struct page **page_data;
	spinlock_t buffer_lock;
};


extern struct pref_buffer prefetch_buffer;
extern int get_buffer_tail(void);
extern int get_buffer_size(void);
extern void inc_buffer_head(void);
extern void inc_buffer_tail(void);
extern void inc_buffer_size(void);
extern void dec_buffer_size(void);
extern int is_buffer_full(void);

#endif /*RING_BUFFER_H*/
