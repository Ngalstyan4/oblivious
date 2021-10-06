// #include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

#include <linux/fs.h> // Needed by filp
#include <asm/uaccess.h> // Needed by segment descriptors

#include "common.h"

// /data/traces/[trace_id].bin.[thread_ind]
const char *RECORD_FILE_FMT = "/data/traces/%s/main.bin.%d";
const char *FETCH_FILE_FMT = "/data/traces/%s/main.tape.%d";

const unsigned long PAGE_ADDR_MASK = ~0xfff;
const unsigned long PRESENT_BIT_MASK = 1UL;
const unsigned long SPECIAL_BIT_MASK = 1UL << 58;

atomic_t metronome = ATOMIC_INIT(0);
#ifdef DEBUG_FS
struct dentry *debugfs_root;
#endif

pte_t *addr2pte(unsigned long addr, struct mm_struct *mm)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	// walk the page table ` https://lwn.net/Articles/106177/
	//todo:: pteditor does it wrong i think,
	//it does not dereference pte when passing around
	pgd = pgd_offset(mm, addr);
	if (unlikely(pgd_none(*pgd) || pgd_bad(*pgd)))
		return pte;
	pud = pud_offset(pgd, addr);
	if (unlikely(pud_none(*pud) || pud_bad(*pud)))
		return pte;
	pmd = pmd_offset(pud, addr);

	// todo::investigate
	// currently, in trace recording pmd is none when
	// the page table is in process of being grown
	// this is because we intercept kernel page fault *before*
	// it grows page tables and is ready to assign ptes.
	// this, I think, means that we miss memory accesses which are
	// alligned to page table boundaries and since we do not
	// add special bit to these addresses in the very beginning,
	// we never add them to the trace record

	// todo:: to support thp, do some error checking here and see if a huge page is being allocated
	if (unlikely(pmd_none(*pmd) || pud_large(*pud)))
		return pte;

	//todo:: pte_offset_map_lock<-- what is this? when whould I need to take a lock?
	pte = pte_offset_map(pmd, addr);

	return pte;
}

pte_t *addr2ptepmd(unsigned long addr, struct mm_struct *mm, pmd_t **pmd_ret)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return pte;
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return pte;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pud_large(*(pud)))
		return pte;
	pte = pte_offset_map(pmd, addr);
	*pmd_ret = pmd;
	return pte;
}

bool proc_file_exists(const char *proc_name, const char *path_fmt, int tid)
{
	char trace_filepath[FILEPATH_LEN];

	snprintf(trace_filepath, FILEPATH_LEN, path_fmt, proc_name, tid);

	// in case path is too long, truncate;
	trace_filepath[FILEPATH_LEN - 1] = '\0';
	return file_exists(trace_filepath);
}

static bool file_stat(const char *filepath, struct kstat *stat_struct)
{
	bool err;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // kernel_ds
	err = vfs_stat(filepath, stat_struct);
	set_fs(old_fs);
	return err;
}

bool file_exists(const char *filepath)
{
	struct kstat trace_stat;
	return 0 == file_stat(filepath, &trace_stat);
}

size_t file_size(const char *filepath)
{
	struct kstat trace_stat;
	if (file_stat(filepath, &trace_stat) != 0)
		return -1;
	return trace_stat.size;
}

size_t read_tape(const char *filepath, char *buf, long max_len)
{
	struct file *f;
	size_t count = 0;
	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS
	f = filp_open(filepath, O_RDONLY | O_LARGEFILE, 0);
	if (f == NULL) {
		printk(KERN_ERR "unable to open %s for reading", filepath);
		goto fail;
	}

	while (count < max_len) {
		ssize_t rv = vfs_read(f, &buf[count], max_len - count, &f->f_pos);
		if (rv == 0) {
			goto success;
		} else if (rv < 0) {
			printk(KERN_ERR "unable to read file %s: %d", filepath, (int) rv);
			goto fail;
		}
		count += rv;
	}

	{
		char scratch;
		ssize_t rv = vfs_read(f, &scratch, 1, &f->f_pos);
		if (likely(rv == 0)) {
			goto success;
		} else {
			printk(KERN_ERR "could not read the entire tape: read only %llu bytes", (unsigned long long int) count);
		}
	}

fail:
	count = 0;

success:
	filp_close(f, NULL);
	set_fs(old_fs);
	return count;
}

struct file *open_trace(const char *filepath)
{
	struct file *f;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS

	// todo::mkdir before open
	f = filp_open(filepath, O_CREAT | O_WRONLY | O_LARGEFILE, 0644);
	if (IS_ERR(f)) {
		printk(KERN_ERR
		       "unable to create/open file %s. ERR code: %pe\n",
		       filepath, f);
		f = NULL;
	}

	set_fs(old_fs);
	return f;
}

void close_trace(struct file *f)
{
	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS

	filp_close(f, NULL);

	set_fs(old_fs);
}

void write_buffered_trace_to_file(struct file *f, const char *buf, long len)
{
	long left_to_write = len;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS

	printk(KERN_DEBUG "Writing recorded trace (num accesses=%ld)",
	       len / sizeof(void *));

	while (left_to_write > 0) {
		// cannot write more than 2g at a time from kernel
		// fixed in newer kernels, I guess just upgrade?
		ssize_t count = kernel_write(f, buf, left_to_write, f->f_pos);

		//size_t can not be smaller than zero
		if (count < 0) {
			printk(KERN_ERR "Failed writing. "
					"errno=%ld, left to "
					"write %ld\n",
			       count, left_to_write);
			break;
		}

		f->f_pos += count;
		left_to_write -= count;
		buf += count;
	}

	if (left_to_write > 0) {
		printk(KERN_ERR "unable to write %ld "
				"out of %ld total\n",
		       left_to_write, len);
	}

	set_fs(old_fs);
}

void log_pfault(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, unsigned long pte_val)
{
	printk(KERN_DEBUG "pfault [%s | %s | %s | "
			  "%s | %s]  %lx pte: %lx [%s|%s]\n",
	       error_code & PF_PROT ? "PROT" : "",
	       error_code & PF_WRITE ? "WRITE" : "READ",
	       error_code & PF_USER ? "USER" : "KERNEL",
	       error_code & PF_RSVD ? "SPEC" : "",
	       error_code & PF_INSTR ? "INSTR" : "", address, pte_val,
	       pte_val & PRESENT_BIT_MASK ? "PRESENT" : "",
	       pte_val & SPECIAL_BIT_MASK ? "SPEC" : "");
}
