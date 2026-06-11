#ifndef _COMMON_INCLUDES_SYSCALLS_MEM_H
#define _COMMON_INCLUDES_SYSCALLS_MEM_H

#include <stdint.h>

typedef enum MemOperation
{
    MEM_ALLOC_PAGES,
    MEM_FREE_PAGES,
    MEM_SBRK,
} MemOperation;

typedef enum MemStatus
{
    MEM_OK = 0,
    MEM_NO_PROCESS = -1,
    MEM_INVALID_ADDRESS = -2,
    MEM_INVALID_SIZE = -3,
    MEM_ALLOCATION_FAILED = -4,
    MEM_FREE_FAILED = -5,
    MEM_OPERATION_NOT_SUPPORTED = -6,
} MemStatus;

typedef uint64_t MemSize;
typedef uint64_t MemAddress;
typedef int64_t MemIncrement;

#endif
