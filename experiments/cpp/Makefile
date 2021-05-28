CC=gcc
CXX=g++
EIGEN_DIR=$(realpath ./eigen/Eigen)
CFLAGS=-O0 -Wall -Wextra -g -march=native -fno-omit-frame-pointer
CPPFLAGS=-O2 -Wall -Wextra -g -march=native -fno-omit-frame-pointer -std=c++17 -I $(EIGEN_DIR)
# used for mmap_random+_rw because optimizations optimize out most memory reads..somehow
CPPFLAGS_O0 =-O0 -Wall -Wextra -g -march=native -std=c++17 -static  -I $(EIGEN_DIR)

all: mmult_eigen sparse_eigen mmap_random_rw sort

mmap_random_rw: mmap_random_rw.cpp
	$(CXX) $(CPPFLAGS_O0) $^ -o $@

mmult_eigen: mmult_eigen.cpp
	$(CXX) $(CPPFLAGS) -static $^ -o $@
	$(CXX) $(CPPFLAGS) $^ -o $@_dyn

sparse_eigen: sparse_eigen.cpp
	$(CXX) $(CPPFLAGS) -static $^ -o $@
	$(CXX) $(CPPFLAGS) $^ -o $@_dyn

mmult_eigen_par: mmult_eigen.cpp
	$(CXX) $(CPPFLAGS) $^  -fopenmp  -o $@

sort: sort.cpp
	$(CXX) $(CPPFLAGS) $^ -o $@
	$(CXX) $(CPPFLAGS) -DMADVISE $^ -o $@_madvise

.PHONY: clean
clean:
	rm -rf *.o mmult_eigen mmult_eigen_dyn mmap_random_rw sort sort_madvise

