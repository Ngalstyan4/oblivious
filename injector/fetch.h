#ifndef FETCH_H
#define FETCH_H

void fetch_init(pid_t pid, const char *proc_name, struct mm_struct *mm);
bool fetch_initialized();
void fetch_clone(struct task_struct *p, unsigned long clone_flags);
void fetch_fini();
void fetch_force_clean();
#endif /*FETCH_H*/
