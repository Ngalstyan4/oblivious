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

/* Copied from arch/x86/mm/fault.c. */
/*
 * Page fault error code bits:
 *
 *   bit 0 ==	 0: no page found	1: protection fault
 *   bit 1 ==	 0: read access		1: write access
 *   bit 2 ==	 0: kernel-mode access	1: user-mode access
 *   bit 3 ==				1: use of reserved bit detected
 *   bit 4 ==				1: fault was an instruction fetch
 *   bit 5 ==				1: protection keys block access
 */
enum x86_pf_error_code {
	// clang-format off
	PF_PROT		=		1 << 0,
	PF_WRITE	=		1 << 1,
	PF_USER		=		1 << 2,
	PF_RSVD		=		1 << 3,
	PF_INSTR	=		1 << 4,
	PF_PK		=		1 << 5,
	// clang-format on
};

#define FILEPATH_LEN 256
extern const char *RECORD_FILE_FMT;
extern const char *FETCH_FILE_FMT;

extern const unsigned long PAGE_ADDR_MASK;
extern const unsigned long PRESENT_BIT_MASK;
extern const unsigned long SPECIAL_BIT_MASK;

// N.B. does not take mmap_sem and the caller must take the semaphore
// if need be
pte_t *addr2pte(unsigned long addr, struct mm_struct *mm);
pte_t *addr2ptepmd(unsigned long addr, struct mm_struct *mm, pmd_t **pmd_ret);
bool proc_file_exists(const char *proc_name, const char *path_fmt, int tid);
bool file_exists(const char *filepath);
size_t file_size(const char *filepath);

// does not return anything since we cannot take any action on fail
struct file *open_trace(const char *filepath);
void close_trace(struct file *f);
void write_buffered_trace_to_file(struct file *f, const char *buf, long len);
size_t read_tape(const char *filepath, char *buf, long max_len);

void log_pfault(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, unsigned long pte_val);
#endif /*COMMON_H*/
