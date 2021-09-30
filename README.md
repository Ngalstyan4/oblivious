# Oblivious

## Overview

### What's working
* Most* tracing of C/C++ applications
    * trace is saved at `/data/traces/$APP_NAME`
    * tracer allocates 40G memory to collect the trace in. This can be configured in [injector/common.h](injector/common.h)
* fetching remote memory into local page cache
    * this works better when memory trace does not have many repeating sections since less repeating sections means more minor page faults which is what we use to stay in sync
    * Although it works for the general case as well as long as we properly preprocess the trace first
* evict pages from a separate CPU (no changes from fastswap, unless you change aggressiveness parameter to start evicting before cgroup limit is reached)



### What's not working
* tracing can rarely be unstable. For certain applicaions with certain parameters this happens more often. There are issues with IN_ALT_PATTERN codepath as it is not clear when why this path is taken. We thought this only happens when an instruction accesses a page boundary but number of times the exact same application ends up in ALT PATTERN varies which cannot be explained by the above.
* Tracing python applications works in theory but the generated trace is kind of useless. Python maps a bunch of vmas and because we start tracing as soon as syscall is called and python dynamically loads executable code, we now end up tracing some code pages as well which is useless and also confuses page fault handler. We need to look into vm_area_structs of a python process to only consider faults in the relevant areas for python tracing to work


## Building
* start with ubuntu1604 vm
* Install Mellanox OFED 4.2 on it
```bash
wget www.mellanox.com/downloads/ofed/MLNX_OFED-4.2-1.2.0.0/MLNX_OFED_LINUX-4.2-1.2.0.0-ubuntu16.04-x86_64.tgz
```
* set up subnets, check with `ib_write_bw` that there is rdma connection
* switch to `fastswap branch` and make sure vanilla fastswap is working on the vm
* switch back to `master`
    * For faster kernel builds, install `ccache` and
    ```bash
    # to build the kernel
    time KBUILD_BUILD_HOST='dev_fastswap' KBUILD_BUILD_VERSION=44 KBUILD_BUILD_TIMESTAMP='lunchtime' make CC="ccache gcc" -j12
    # to install the kernel
    sudo make headers_install -j12 && sudo make INSTALL_MOD_STRIP=1 modules_install -j12 && sudo make install -j4
    ```

* restart after installing the kernel
* disable thp and aslr
```bash
echo never | sudo tee  /sys/kernel/mm/transparent_hugepage/enabled
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
echo "aslr : $(cat /proc/sys/kernel/randomize_va_space)"
echo "transparent huge pages: $(cat /sys/kernel/mm/transparent_hugepage/enabled)"

```
* load fastswap drivers
* load injector module
    * [cli.sh](./injector/cli.sh) can be used as a comman line interface for changing parts of the system.
    Every time the module is loaded, it prints a set of variables which show which features are enabled.

    ```
    [Fri Jan 29 00:23:35 2021] resetting injection points to noop
    [Fri Jan 29 00:23:35 2021] USAGE: insmod mem_pattern_trace.ko cmd="$cmd" val="$val"
    [Fri Jan 29 00:23:35 2021] where $cmd is one of mem_trace cli commands (in parentheses below)
    [Fri Jan 29 00:23:35 2021] and $val is the value for commend (usualy 0 or 1) if it requires a value
    [Fri Jan 29 00:23:35 2021] memtrace global flags:
    Tape operations                ON (tape_ops)
    Swap SSD Optim                 ON (ssdopt)
    Fastswap writes                ASYNC (async_writes)
    Tape fetch                     ON (tape_fetch)
    print LRU dmesg logs           OFF (lru_logs)

    ```

    * to disable ssd optimization, then you'll need to run `./cli.sh ssdopt 0`

    * in case of kernel panic, in order to get file name:line number pairs in the stack trace, run `dmesg` as:
    ```
    script --flush --quiet --return /tmp/decode_stacktrace_output.txt --command "dmesg -wT --color=always | ~/oblivious/scripts/decode_stacktrace.sh ~/oblivious/vmlinux ~/oblivious/injector"
    ```


This state is kept outside of the loaded module (in an [kernel struct](mm/injections.c)) so reloading the module without any argument preserves these variables
Most of the system is in [injector](./injector) folder even though it modifies functionality in various parts of the kernel memory subsystem

### Other kernel changes

| File | Description |
| ---- | ----------- |
| [injections](mm/injections.c)| Defines injection array which is used to dynamically change kernel function from [the kernel module](./injector)|
| [arch/x86/mm/fault.c](arch/x86/mm/fault.c)| add injections into the x86 page fault handler so custom module can change its functionality
| [frontswap]( mm/frontswap.c)| Add fastswap async write functions|
| [tracer](./tracer)| defines mem_pattern_trace syscall interface|

### Notes
- in The system currently supports a max of 20 threads per process. This is configurable in `include/linux/task_struct_oblivious.h`
