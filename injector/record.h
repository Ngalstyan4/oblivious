#ifndef RECORD_H
#define RECORD_H

void record_init(pid_t pid, const char *proc_name, unsigned int microset_size);
bool record_initialized();
void record_fini();
void record_force_clean();
#endif /*RECORD_H*/
