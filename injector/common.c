// #include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

#include <linux/fs.h> // Needed by filp
#include <asm/uaccess.h> // Needed by segment descriptors

#include "common.h"

const char *TRACE_FILE_FMT = "/data/traces/%s.bin";

const unsigned long PAGE_ADDR_MASK = ~0xfff;
const unsigned long PRESENT_BIT_MASK = 1UL;
const unsigned long SPECIAL_BIT_MASK = 1UL << 58;

/*
   * Page fault error code bits:
 *
 *   bit 0 ==	 0: no page found	1: protection fault
 *   bit 1 ==	 0: read access		1: write access
 *   bit 2 ==	 0: kernel-mode access	1: user-mode access
 *   bit 3 ==				1: use of reserved bit detected
 *   bit 4 ==				1: fault was an instruction fetch
 */
enum x86_pf_error_code {

	PF_PROT = 1 << 0,
	PF_WRITE = 1 << 1,
	PF_USER = 1 << 2,
	PF_RSVD = 1 << 3,
	PF_INSTR = 1 << 4,
};

pte_t *addr2pte(unsigned long addr, struct mm_struct *mm)
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
	if (pmd_none(*(pmd)) || pud_large(*(pud)))
		return pte;
	pte = pte_offset_map(pmd, addr);

	// printk(KERN_WARNING "page walk ahead %d %s %lx", i,
	//        !pte_none(*vm_entry.pte) &&
	// 		       pte_present(*vm_entry.pte) ?
	// 	       "PRESENT" :
	// 	       "GONE",
	//        addr & PAGE_ADDR_MASK);
	return pte;
}

bool proc_trace_exists(const char *proc_name)
{
	char trace_filepath[FILEPATH_LEN];

	snprintf(trace_filepath, FILEPATH_LEN, TRACE_FILE_FMT, proc_name);

	// in case path is too long, truncate;
	trace_filepath[FILEPATH_LEN - 1] = '\0';
	return file_exists(trace_filepath);
}

bool file_exists(const char *filepath)
{

	struct kstat trace_stat;
	bool trace_exists;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS
	trace_exists = 0 == vfs_stat(filepath, &trace_stat);
	set_fs(old_fs);
	return trace_exists;
}

size_t read_trace(const char *filepath, char *buf, long max_len)
{
	struct file *f;
	size_t count = 0;
	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS
	f = filp_open(filepath, O_RDONLY | O_LARGEFILE, 0);
	if (f == NULL) {
		printk(KERN_ERR "unable to read/open file\n");
		set_fs(old_fs);
		return 0;
	}

	count = vfs_read(f, buf, max_len, &f->f_pos);
	filp_close(f, NULL);
	set_fs(old_fs);
	return count;
}
void write_trace(const char *filepath, const char *buf, long len)
{
	long left_to_write = len;
	// Write recorded trace to file
	// docs ` https://www.howtoforge.com/reading-files-from-the-linux-kernel-space-module-driver-fedora-14
	// https://www.linuxjournal.com/article/8110
	struct file *f;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds()); // KERNEL_DS

	f = filp_open(filepath, O_CREAT | O_WRONLY | O_LARGEFILE, 0644);
	if (IS_ERR(f)) {
		printk(KERN_ERR "unable to create/open file ERR: %ld\n",
		       PTR_ERR(f));
		return;
	}

	printk(KERN_DEBUG "Writing recorded trace (num accesses=%ld)",
	       len / sizeof(void *));
	while (left_to_write > 0) {
		// cannot write more than 2g at a time from kernel
		// fixed in newer kernels, I guess just upgrade?
		size_t count = vfs_write(f, buf, left_to_write, &f->f_pos);

		//size_t can not be smaller than zero
		if (((long)(count)) < 0) {
			printk(KERN_ERR "Failed writing. "
					"errno=%ld, left to "
					"write %ld\n",
			       count, left_to_write);
			break;
		}
		printk(KERN_DEBUG "wrote %ld bytes out of %ld "
				  "left to write and %ld total\n",
		       count, left_to_write, len);
		left_to_write -= count;
		buf += count;
	}

	filp_close(f, NULL);
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
