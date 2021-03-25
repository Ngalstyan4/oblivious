#ifndef FETCH_H
#define FETCH_H

void fetch_init(struct task_struct *tsk, int flags);
bool fetch_initialized();
void fetch_page_fault_handler(struct pt_regs *regs, unsigned long error_code,
			    unsigned long address, struct task_struct *tsk,
			    bool *return_early, int magic);
void fetch_clone(struct task_struct *p, unsigned long clone_flags);
void fetch_fini(struct task_struct *tsk);
#endif /*FETCH_H*/
