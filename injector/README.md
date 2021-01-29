# Injector
## Kernel module used to reload parts of the kernel

Kernel exports [`set_pointer(int index, func* f)`](https://github.com/Ngalstyan4/oblivious/blob/master/include/linux/injections.h#L5-L7) updates `index` element of a global function array. The function initially contains `noop`s and is called from all points of potential interest inside the kernel.

### File structure

| File | Description |
| ---- | ----------- | 
| [mem_pattern_trace.h](mem_pattern_trace.h)| Public syscall interface that must be included by applications that wish to be `mem_pattern_trace`d.
| [module_entry.c](module_entry.c)| Boilerplate for setting up kernel module. Sets up all kernel injections which need to be done upon module load/reload (e.g. `mem_pattern_trace` syscall setup code)|
| [record.c](record.c)| Implements memory memory access pattern recording for a process. A direct copy from [Chris' implementation](https://github.com/chrisaugmon/Prefetching/blob/narek/userpagefault/utils.cpp#L43-L260) with kernel API instead of PTEditor + kernel signals|
| [record.h](record.h) | provides `record_init`, `record_fini` API that starts, cleans up after trace recording|
| [fetch.[ch]](fetch.h) | provides `fetch_init`, `fetch_fini` API that starts, cleans up after tape prefetching|
| [common.[ch]](common.h) | Common constants and general utility functions used among {record,prefetch,evict}|
| [evict.[ch]](evict.c) | Controlls how aggressively fastswap evicts pages by controlling high_work_func trigger threshhold|
| [kevictd.[ch]](kevictd.c) | Dead code from earlier version where I tried offloading evictions into a dedicated kernel thread which I can schedule without using kernel scheduling queues. Still here as code may be useful for evicting fetching|
| [fastswap_bench.h](fastswap_bench.h) | exposes `fastswap_bench` function which can be configured to benchmark sync and async fastswap write throughput. The function is hooked to `mem_pattern_trace` syscall with TRACE_MISC flag.|


