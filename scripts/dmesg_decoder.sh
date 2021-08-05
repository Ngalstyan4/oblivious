#!/bin/bash

script --flush --quiet --return /tmp/decode_stacktrace_output.txt --command "dmesg -wT --color=always | /mydata/oblivious/scripts/decode_stacktrace.sh /mydata/oblivious/vmlinux /mydata/oblivious/injector"
