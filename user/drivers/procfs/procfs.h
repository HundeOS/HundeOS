#ifndef HUND_USER_PROCFS_H
#define HUND_USER_PROCFS_H
#include "process/process_registry.h"
#include "io/filesystem_driver.h"
#include "syscalls/system_info.h"

typedef Result(KernelTraceSize) (*ProcfsKernelTraceLoad)(uint8_t * destination, size_t capacity);

typedef struct ProcfsContext
{
    ConstProcessRegistryPtr registry;
    ProcfsKernelTraceLoad load_kernel_trace;
} ProcfsContext;

typedef ProcfsContext * ProcfsContextPtr;

static inline ProcfsContext ProcfsContext__new(ConstProcessRegistryPtr registry,
                                               ProcfsKernelTraceLoad load_kernel_trace)
{
    return (ProcfsContext){.registry = registry, .load_kernel_trace = load_kernel_trace};
}

FilesystemOperations Procfs__operations(void);
#endif
