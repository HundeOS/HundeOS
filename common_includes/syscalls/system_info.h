#ifndef HUND_SYSCALLS_SYSTEM_INFO_H
#define HUND_SYSCALLS_SYSTEM_INFO_H

#include <stdbool.h>
#include <stddef.h>

#include "types/result.h"

#define KERNEL_TRACE_CAPACITY (12U * 1024U)

typedef size_t KernelTraceSize;
ResultDef(KernelTraceSize);

typedef enum SystemInfoOperation
{
    SYSTEM_INFO_READ_SETTINGS = 1,
    SYSTEM_INFO_READ_KERNEL_TRACE = 2,
} SystemInfoOperation;

typedef struct SystemSettings
{
    bool io_latency_tracing_enabled;
} SystemSettings;

typedef SystemSettings * SystemSettingsPtr;
typedef const SystemSettings * ConstSystemSettingsPtr;
ResultDef(SystemSettings);

static inline SystemSettings SystemSettings__new(bool io_latency_tracing_enabled)
{
    return (SystemSettings){.io_latency_tracing_enabled = io_latency_tracing_enabled};
}

typedef enum SystemInfoResult
{
    SYSTEM_INFO_OK = 0,
    SYSTEM_INFO_INVALID_ARGUMENT = -1,
    SYSTEM_INFO_PERMISSION_DENIED = -2,
    SYSTEM_INFO_UNAVAILABLE = -3,
} SystemInfoResult;

#endif
