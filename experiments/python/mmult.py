import gc
gc.disable()

# put before importing np so it uses single thread
# https://stackoverflow.com/questions/30791550/limit-number-of-threads-in-numpy
import os
os.environ["MKL_NUM_THREADS"] = "1"
os.environ["NUMEXPR_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"

import numpy as np
from mem_pattern_trace import *
import torch
import torchvision
import torchvision.models as models
SIZE=4096

x = np.random.rand(1,1)
x = np.matmul(x,x)
np.random.seed(6)
syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO)
a = np.random.rand(SIZE,SIZE)
b = np.random.rand(SIZE,SIZE)
c = np.matmul(a,b)
syscall(mem_pattern_trace, TRACE_END)
print ("Result: %d" % c.sum())

