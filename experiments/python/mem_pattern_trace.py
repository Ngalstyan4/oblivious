## IMPORTANT!!! THIS FILE MUST BE KEPT IN SYNC WITH mem_pattern_trace.h !!!
import ctypes
libc = ctypes.CDLL(None)
syscall = libc.syscall

TRACE_START = 1 << 0
TRACE_PAUSE = 1 << 1
TRACE_RESUME = 1 << 2
TRACE_END = 1 << 3
TRACE_RECORD = 1 << 4
TRACE_PREFETCH = 1 << 5
TRACE_AUTO = 1 << 6
KEVICTD_INIT = 1<<7
KEVICTD_FINI = 1<<8

mem_pattern_trace = 333
