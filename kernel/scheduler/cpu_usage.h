#ifndef KERNEL_SCHEDULER_CPU_USAGE_H
#define KERNEL_SCHEDULER_CPU_USAGE_H

#include "../mutex.h"
#include "../types/fixed_array.h"
#include "../../common_includes/syscalls/process.h"

typedef uint64_t CPUTime;
typedef uint64_t CPUUsageTick;

typedef struct CPUUsageBucket
{
    CPUUsageTick tick;
    CPUTime usec;
} CPUUsageBucket;
typedef CPUUsageBucket * CPUUsageBucketPtr;
OptionalDef(CPUUsageBucket);

bool CPUUsageBucket__equals(CPUUsageBucketPtr self, CPUUsageBucketPtr other);
FixedArrayDefine(CPUUsageBucket, PROCESS_CPU_LONG_TICKS + 1, CPUUsageBuckets, (EqualFunction)CPUUsageBucket__equals);

typedef struct CPUUsage
{
    mutex_t lock;
    CPUTime total_usec;
    CPUTime started_at;
    bool running;
    CPUUsageBuckets buckets;
} CPUUsage;
typedef CPUUsage * CPUUsagePtr;

CPUUsage CPUUsage__new(void);
void CPUUsage__start(CPUUsagePtr self);
void CPUUsage__stop(CPUUsagePtr self);
ProcessCPUUsage CPUUsage__snapshot(CPUUsagePtr self);

#endif
