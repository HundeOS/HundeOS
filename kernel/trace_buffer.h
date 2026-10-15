#ifndef HUND_KERNEL_TRACE_BUFFER_H
#define HUND_KERNEL_TRACE_BUFFER_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "syscalls/system_info.h"
#include "types/result.h"

typedef struct KernelTraceBuffer
{
    atomic_flag lock;
    size_t write_position;
    size_t count;
    uint8_t storage[KERNEL_TRACE_CAPACITY];
} KernelTraceBuffer;

typedef KernelTraceBuffer * KernelTraceBufferPtr;
typedef const KernelTraceBuffer * ConstKernelTraceBufferPtr;

#define KERNEL_TRACE_BUFFER_INIT                                                                                       \
    (KernelTraceBuffer)                                                                                                \
    {                                                                                                                  \
        .lock = ATOMIC_FLAG_INIT,                                                                                      \
    }

void KernelTraceBuffer__append(KernelTraceBufferPtr self, const char * text);
Result(KernelTraceSize) KernelTraceBuffer__snapshot(KernelTraceBufferPtr self, uint8_t * destination, size_t capacity);

void KernelTrace__append(const char * text);
Result(KernelTraceSize) KernelTrace__snapshot(uint8_t * destination, size_t capacity);

#endif
