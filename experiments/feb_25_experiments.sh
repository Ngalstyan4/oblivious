#!/bin/bash
#args:./benchmark.sh [results_dir] [RSS_in_pages]             [command to run in cgroup under memory pressure]
sudo ./benchmark.sh bitonic_merge $((1500+ 8*(1<<28)/4096))  taskset -c 0 /home/narekg/Prefetching/sorting/sort $((1<<28)) 42  bitonic_merge false
sudo ./benchmark.sh bitonic_sort  $((1500+ 8*(1<<25)/4096))  taskset -c 0 /home/narekg/Prefetching/sorting/sort $((1<<25)) 42  bitonic_sort false
sudo ./benchmark.sh mmult_eigen 135000 		             taskset -c 0 ./cpp/mmult_eigen 4 4096 mat
sudo ./benchmark.sh vec_eigen 528000 		             taskset -c 0 ./cpp/mmult_eigen 4 $((4096*4)) vec
sudo ./benchmark.sh dot_eigen 528000 		             taskset -c 0 ./cpp/mmult_eigen 4 $((4096*4096*8)) dot
sudo ./benchmark.sh mmap_random_rw 400000 		     taskset -c 0 ./cpp/mmap_random_rw 4 400000 1200000 w
sudo ./benchmark.sh kissfft     293064  taskset -c 0 /home/narekg/oblivious/experiments/cpp/kissfft/build/test/bm_kiss-int16_t -x 1 -n 100,100,100,100
sudo ./benchmark.sh kmeans 145000 			    taskset -c 0 /home/narekg/miniconda2/bin/python ./python/kmeans.py
