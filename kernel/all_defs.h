#ifndef __KERNEL_ALL_DEFS_H
#define __KERNEL_ALL_DEFS_H

#define PAGE_SIZE 4096
#define MAX_CPUS 16
#define STACK_SIZE (1024 * 1024 * 10)
#define BASE_STACK 0x10000000
#define MAX_PROCESSES 2048
#define IO_MAX_DEVICES 256
#define MAX_HANDLES_PER_DEVICE 256
#define MAX_HANDLES_PER_PROCESS 1024
#define MAX_HANDLES 4096
#define MAX_PARKED_CONTEXTS MAX_HANDLES

#define MAX_MOUNTS 128
#define MAX_CHILD_NODES 1024

#define MAX_SPINS 100000000

#define _128KB (128 * 1024)
#define _1MB (1024 * 1024)
#define _2MB (2 * 1024 * 1024)
#define _4MB (4 * 1024 * 1024)
#define _8MB (8 * 1024 * 1024)
#define _16MB (16 * 1024 * 1024)
#define _1GB (1024 * _1MB)

#define _4GB = 4 * _1GB;

#endif