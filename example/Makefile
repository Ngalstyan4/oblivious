# docs here ` https://linux.die.net/lkmpg/x351.html
obj-m += mem_pattern_trace.o
mem_pattern_trace-objs := page_buffer.o ring_buffer.o kevictd.o leap_functionality.o utils.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
