#ifndef __KERNEL_MEM_H
#define __KERNEL_MEM_H
#include <stdint.h>

#define PAGE_SIZE 4096
typedef struct {
    void* base;
    uint64_t size;
} Stack;

#endif