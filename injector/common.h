#ifndef COMMON_H
#define COMMON_H

// enable debugfs monitoring hooks of the module
// see debugfs docs here `
// https://www.kernel.org/doc/html/latest//filesystems/debugfs.html
#define DEBUG_FS 1

#if DEBUG_FS
#include <linux/debugfs.h>
extern struct dentry *debugfs_root;
#endif

// atomic counter advanced by evict.c ONLY.
// Used to synchronize lru related print
// statements from different parts of the module
// for coherent post processing
extern atomic_t metronome;

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
size_t file_size(const char *filepath);

// does not return anything since we cannot take any action on fail
void write_trace(const char *filepath, const char *buf, long len);
size_t read_trace(const char *filepath, char *buf, long max_len);

void log_pfault(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, unsigned long pte_val);
#endif /*COMMON_H*/
