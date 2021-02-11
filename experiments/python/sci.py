from sklearn.neural_network import MLPClassifier
import numpy as np
from mem_pattern_trace import *


np.random.seed(44)

syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO)
X = [[0., 0.], [1., 1.]]
y = [0, 1]
clf = MLPClassifier(solver='lbfgs', alpha=1e-5,
                            hidden_layer_sizes=(200, 250, 25, 100), random_state=1)

clf.fit(X, y)
clf.predict(X)
syscall(mem_pattern_trace, TRACE_END)
