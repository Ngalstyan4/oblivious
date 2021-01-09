#ifndef INJECTIONS_H
#define INJECTIONS_H

typedef void (*injected_func_type)();
extern injected_func_type pointers[100];
extern void kernel_noop(void);
extern void set_pointer(int i, void(*f)());
#endif /* INJECTIONS_H */
