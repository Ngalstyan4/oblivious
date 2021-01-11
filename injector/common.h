#ifndef UTILS_H
#define UTILS_H

#define FILEPATH_LEN 256
extern const char *TRACE_FILE_FMT;

extern const unsigned long PAGE_ADDR_MASK;
extern const unsigned long PRESENT_BIT_MASK;
extern const unsigned long SPECIAL_BIT_MASK;

// N.B. does not take mmap_sem and the caller must take the semaphore
// if need be
pte_t *addr2pte(unsigned long addr, struct mm_struct *mm);
bool proc_trace_exists(const char *proc_name);
bool file_exists(const char *filepath);

// does not return anything since we cannot take any action on fail
void write_trace(const char *filepath, const char *buf, long len);
bool read_trace(const char *filepath, char *buf, long max_len);

void log_pfault(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, unsigned long pte_val);
#endif /*UTILS_H*/
