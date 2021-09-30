#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/injections.h>

#include "mem_pattern_trace.h"
#include "common.h"
#include "record.h"
#include "fetch.h"
#include "evict.h"
#include "fastswap_bench.h"

MODULE_AUTHOR("");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("");
static char *cmd, *val;
static int us_size = 2;

MODULE_PARM_DESC(cmd, "Command to properly change mem_trace system");
MODULE_PARM_DESC(val, "Value(usually 0 or 1) required for certain commands");
MODULE_PARM_DESC(us_size, "Microset size in pages (default is 2)");
module_param(cmd, charp, 0000);
module_param(val, charp, 0000);
module_param(us_size, int, 0000);

void mem_pattern_trace_start(int flags)
{
	pid_t pid = current->pid;
	const char *proc_name = current->comm;

	// add the special tag to tell this process apart
	flags |= OBLIVIOUS_TAG;
	if (flags & TRACE_AUTO) {
		if (proc_file_exists(proc_name, FETCH_FILE_FMT, 0))
			flags |= TRACE_PREFETCH;
		else if (!proc_file_exists(proc_name, RECORD_FILE_FMT, 0)) {
			flags |= TRACE_RECORD;
		} else {
			printk(KERN_ERR
			       "trace recoding for process with pid %d and "
			       "name %s exists\n"
			       "but a post processed tape does not exist\n"
			       "Unable to AUTO_(RECORD|PREFETCH)\n",
			       pid, proc_name);
			return;
		}
	}

	printk(KERN_INFO "%s%s%s for PROCESS with pid %d\n",
	       flags & TRACE_AUTO ? "AUTO-" : "",
	       flags & TRACE_RECORD ? "RECORDING" : "",
	       flags & TRACE_PREFETCH ? "PREFETCHING" : "", pid);

	if (flags & TRACE_RECORD) {
		if (us_size < 2) {
			printk(KERN_WARNING "TRACE_RECORD: ALT_PATTERN problem"
					    "may arrise when us_size < 2\n");
		}
		record_init(current, flags, us_size);

	} else if (flags & TRACE_PREFETCH) {
		fetch_init(current, flags);
	}
}

static void mem_pattern_trace_end(int flags)
{
	current->obl.flags = 0;
	// all _fini functions check whether they have been initialized
	// before performing any free-ing so no need to do it here
	fetch_fini(current);
	record_fini(current);
}

static void copy_process_40(struct task_struct *p, unsigned long clone_flags,
			    unsigned long stack_start, unsigned long stack_size,
			    int __user *child_tidptr, struct pid *pid,
			    int trace, unsigned long tls, int node)

{
	/* p is being copy-ed from current. Need to
	 * reset obl state and create its own
	 * p->group_leader is the thread that first
	 * called mem_pattern_trace syscall in a multithreaded process
	 */
	if (!(p->group_leader->obl.flags & OBLIVIOUS_TAG))
		return;

	memset(&p->obl, 0, sizeof(struct task_struct_oblivious));
	if (current->obl.flags & TRACE_PREFETCH)
		fetch_clone(p, clone_flags);
	else if (current->obl.flags & TRACE_RECORD)
		record_clone(p, clone_flags);
}

static void do_page_fault_2(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address, struct task_struct *tsk,
			    bool *return_early, int magic)
{
	/* For performance reasons handler functions do not check whether the process
	 * is in relevant (RECORD|FETCH) mode so this check is important
	 * We really care about perormance in FETCH branch, hence the `likely`
	 * tracing is quite slow so branch misprediction here will not hurt much?
	 * */
	if (likely(current->obl.flags & TRACE_PREFETCH))
		fetch_page_fault_handler(regs, error_code, address, tsk,
					 return_early, magic);
	else if (current->obl.flags & TRACE_RECORD)
		record_page_fault_handler(regs, error_code, address, tsk,
					  return_early, magic);
}

static void do_exit_41()
{
	if (!(current->obl.flags & OBLIVIOUS_TAG))
		return;
	mem_pattern_trace_end(0);
}

// used in the mechanism to keep prefetched pages in cache before first use
static void do_swap_page_50(struct page *page, struct vm_fault *vmf,
			    swp_entry_t entry, struct mem_cgroup *memcg)
{
	if (page && test_bit(PG_unevictable, &page->flags))
		clear_bit(PG_unevictable, &page->flags);
}

static void do_swap_page_end_52(struct page *page, struct vm_fault *vmf,
				swp_entry_t entry, struct mem_cgroup *memcg,
				struct vm_area_struct *vma)
{
}
// if PTE is not present (in swap space/disc) and the application free()s
// it, the page fault handler is not invoked to avoid unnecesary swap
// space disk. that is why cleaning magic bits in page fault handler only
// is not enough and we need to make sure that we clear the magic bit in
// unmap calls *before* the kernel assumes there is corresponding swap entry
// and goes looking for it (in order to free it)
static void do_unmap_5(pte_t *pte)
{
	unsigned long pte_deref_value = native_pte_val(*pte);
	if (pte_deref_value & SPECIAL_BIT_MASK) {
		pte_deref_value |= PRESENT_BIT_MASK;
		pte_deref_value &= ~SPECIAL_BIT_MASK;
		// todo:: this treats the symptom of a present-marked NULL pointer
		// find the cause later
		if ((pte_deref_value & ~PRESENT_BIT_MASK) == 0)
			pte_deref_value = 0;
		set_pte(pte, native_make_pte(pte_deref_value));
	}
}

static void mem_pattern_trace_3(int flags)
{
	if (memtrace_getflag(TAPE_OPS) == 0) {
		printk(KERN_INFO "mem_pattern_trace: functionality is off\n");
		return;
	}

	printk(KERN_INFO "mem_pattern_trace called from pid %d with flags"
			 "[%s%s%s%s|%s%s%s]\n",
	       current->pid, flags & TRACE_START ? "TRACE_START" : "",
	       flags & TRACE_PAUSE ? "TRACE_PAUSE" : "",
	       flags & TRACE_RESUME ? "TRACE_RESUME" : "",
	       flags & TRACE_END ? "TRACE_END" : "",
	       flags & TRACE_RECORD ? "TRACE_RECORD" : "",
	       flags & TRACE_PREFETCH ? "TRACE_PREFETCH" : "",
	       flags & TRACE_AUTO ? "TRACE_AUTO" : ""

	       );

	// for use in miscellaneous experiments
	if (flags & TRACE_MISC) {
		fastswap_bench();
		return;
	}

	if (flags & TRACE_START) {
		mem_pattern_trace_start(flags);
		return;
	}

	if (flags & TRACE_END) {
		mem_pattern_trace_end(flags);
		return;
	}
	if (flags & KEVICTD_INIT) {
		//kevictd_init();
		return;
	}
	if (flags & KEVICTD_FINI) {
		//kevictd_fini();
		return;
	}
}

static void print_memtrace_flags()
{
	printk(KERN_INFO "memtrace global flags:\n"
			 "%-30s %d (%s)\n"
			 "%-30s %s (%s)\n"
			 "\n"

			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "%-30s %s (%s)\n"
			 "\n"
			 "%-30s %s (%s)\n",
	       // clang-format off
		"Microset size", us_size, "us_size",
		"Single tape in multicore", memtrace_getflag(ONE_TAPE) ? "ON" : "OFF", "one_tape",

		"Fastswap", static_branch_unlikely(&frontswap_enabled_key) ? "ON" : "OFF", "fastswap",
		"Tape operations", memtrace_getflag(TAPE_OPS) ? "ON" : "OFF", "tape_ops",
		"Swap SSD Optim",  memtrace_getflag(SWAP_SSD_OPTIMIZATION) ? "ON" : "OFF", "ssdopt",

		"Fastswap writes",  memtrace_getflag(FASTSWAP_ASYNCWRITES) ? "ASYNC" : "SYNC", "async_writes",
		"Enable tape fetch",  memtrace_getflag(TAPE_FETCH) ? "ON" : "OFF", "tape_fetch",
		"Offload prefetching",  memtrace_getflag(OFFLOAD_FETCH) ? "ON" : "OFF", "offload_fetch",
		"Mark unevictable",  memtrace_getflag(MARK_UNEVICTABLE) ? "ON" : "OFF", "unevictable",

		"print LRU dmesg logs",  memtrace_getflag(LRU_LOGS) ? "ON" : "OFF", "lru_logs"
	       // clang-format on
	       );
}

static void usage(void)
{
	printk(KERN_ERR "USAGE: insmod mem_pattern_trace.ko cmd=\"$cmd\" "
			"val=\"$val\" us_size=\"$us_size\"");
	printk(KERN_INFO "where $cmd is one of mem_trace cli commands (in "
			 "parentheses below)");
	printk(KERN_INFO "and $val is the value for commend (usualy 0 or 1) if "
			 "it requires a value\n");
	print_memtrace_flags();
}

static int __init mem_pattern_trace_init(void)
{
	set_pointer(3, mem_pattern_trace_3);

	set_pointer(5, do_unmap_5);
	set_pointer(6, do_unmap_5); //<-- for handle_pte_fault
	set_pointer(2, do_page_fault_2);

	set_pointer(40, copy_process_40);
	set_pointer(41, do_exit_41);

	// assuming the application has no unevictable pages so if we an
	// unevictable-barked page, we remove the marking
	set_pointer(50, do_swap_page_50);
	if (!cmd) {
		usage();
		return -1;
	}

#if DEBUG_FS
	debugfs_root = debugfs_create_dir("memtrace", NULL);
#endif

	if (strcmp(cmd, "fastswap") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? static_branch_enable(&frontswap_enabled_key) :
			      static_branch_disable(&frontswap_enabled_key);
	} else if (strcmp(cmd, "one_tape") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(ONE_TAPE) :
			      memtrace_clearflag(ONE_TAPE);
	} else if (strcmp(cmd, "tape_ops") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(TAPE_OPS) :
			      memtrace_clearflag(TAPE_OPS);
	} else if (strcmp(cmd, "ssdopt") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(SWAP_SSD_OPTIMIZATION) :
			      memtrace_clearflag(SWAP_SSD_OPTIMIZATION);
	} else if (strcmp(cmd, "async_writes") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		if (*val == '0') {
			printk(KERN_ERR
			       "Synchronous Fastswap writes are not supported "
			       "since"
			       "RDMA writes switch to being interrupt based."
			       "TODO:: bring back the option maybe?");
			WARN_ON(true);
			return 0;
		}
		*val == '1' ? memtrace_setflag(FASTSWAP_ASYNCWRITES) :
			      memtrace_clearflag(FASTSWAP_ASYNCWRITES);
	} else if (strcmp(cmd, "tape_fetch") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(TAPE_FETCH) :
			      memtrace_clearflag(TAPE_FETCH);
	} else if (strcmp(cmd, "offload_fetch") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(OFFLOAD_FETCH) :
			      memtrace_clearflag(OFFLOAD_FETCH);
	} else if (strcmp(cmd, "unevictable") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(MARK_UNEVICTABLE) :
			      memtrace_clearflag(MARK_UNEVICTABLE);
	} else if (strcmp(cmd, "lru_logs") == 0) {
		if (!val || (*val != '0' && *val != '1')) {
			usage();
			return 0;
		}

		*val == '1' ? memtrace_setflag(LRU_LOGS) :
			      memtrace_clearflag(LRU_LOGS);
	} else {
		usage();
		return 0;
	}
	print_memtrace_flags();

	// sanity checks
	if (!memtrace_getflag(TAPE_OPS) && memtrace_getflag(TAPE_FETCH)) {
		printk(KERN_WARNING "Cannot fetch as memtrace syscall ops are "
				    "off(tape_ops)\n");
	}

	if (memtrace_getflag(OFFLOAD_FETCH) &&
	    !memtrace_getflag(MARK_UNEVICTABLE)) {
		printk(KERN_WARNING "Offloading evictions without marking "
				    "prefetched pages unevictable"
				    "will result in VERY poor performance\n");
	}
	return 0;
}

static void __exit mem_pattern_trace_exit(void)
{
	int i;

	printk(KERN_DEBUG "resetting injection points to noop");
	for (i = 0; i < 100; i++)
		set_pointer(i, kernel_noop);

#if DEBUG_FS
	debugfs_remove_recursive(debugfs_root);
#endif
}

module_init(mem_pattern_trace_init);
module_exit(mem_pattern_trace_exit);
