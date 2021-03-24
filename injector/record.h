#ifndef RECORD_H
#define RECORD_H

void record_init(struct task_struct *tsk, int flags, unsigned int microset_size);
bool record_initialized(struct task_struct *tsk);
void record_clone(struct task_struct *p, unsigned long clone_flags);
void record_fini(struct task_struct *tsk);
void record_force_clean();
#endif /*RECORD_H*/
