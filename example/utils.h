#ifndef UTILS_BUFFER_H
#define UTILS_BUFFER_H

// N.B. does not take mmap_sem and the caller must take the semaphore
// if need be
pte_t *addr2pte(unsigned long addr, struct mm_struct *mm);

#endif /*UTILS_BUFFER_H*/
