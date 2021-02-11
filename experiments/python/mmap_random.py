from random import randint,seed
from mem_pattern_trace import *

seed(42)

syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO)
arr = [0] * 4096
for i in range(10000):
    arr[randint(0,4096*10000)] = 0xff

syscall(mem_pattern_trace, TRACE_END)
