#ifndef __KERNEL_ALL_DEFS_H
#define __KERNEL_ALL_DEFS_H

#define PAGE_SIZE 4096
#define MAX_CPUS 16
#define STACK_SIZE (1024 * 1024 * 2)
#define BASE_STACK 0x10000000
#define MAX_PROCESSES 65536
#define IO_MAX_DEVICES 256
#define MAX_HANDLES_PER_DEVICE 256

#define MAX_SPINS 100000000

#define _1MB (1024 * 1024)
#define _2MB (2 * 1024 * 1024)
#define _4MB (4 * 1024 * 1024)
#define _1GB (1024 * _1MB)

#define _4GB = 4 * _1GB;

#endif