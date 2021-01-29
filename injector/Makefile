# docs here ` https://linux.die.net/lkmpg/x351.html
obj-m += mem_pattern_trace.o
mem_pattern_trace-objs := common.o
mem_pattern_trace-objs += record.o fetch.o
mem_pattern_trace-objs += evict.o #kevictd.o
mem_pattern_trace-objs += module_entry.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
