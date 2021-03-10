## Example worklaod to test with fastswap under heavy memory pressure

When running this workload under memory pressure (<200MG of local memory, RSS=~520MB) with taskset -c `0,1,2,3,4,5,6`, one of the openMP threads often stalls in D state.
This is not happening when threads are pinned to cpus (with GOMP flagi, as seen below)

Steps to build:
```
# download libtorch library
wget https://download.pytorch.org/libtorch/nightly/cpu/libtorch-shared-with-deps-latest.zip
unzip libtorch-shared-with-deps-latest.zip

# build the executable
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH=/absolute/path/to/libtorch ..
cmake --build . --config Release
```

Run
```
sudo GOMP_CPU_AFFINITY="1-6" OMP_SCHEDULE=static OMP_NUM_THREADS=6 taskset -c 1,2,3,4,5,6 ./cpp/torch_example/build/example-app
```

You can also run the whole benchmark suit, which measures performance under different memory ratios, with:
```
sudo GOMP_CPU_AFFINITY="1-6" OMP_SCHEDULE=static OMP_NUM_THREADS=6 ./benchmark.sh torch_alexnet_par_async 135000 taskset -c 1,2,3,4,5,6 ./cpp/torch_example/build/example-app
```

`benchmark.sh` is in oblivious/experiments. Run the script without arguments to get the usage example and meanings of all arguments
