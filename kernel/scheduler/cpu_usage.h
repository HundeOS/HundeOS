#ifndef KERNEL_SCHEDULER_CPU_USAGE_H
#define KERNEL_SCHEDULER_CPU_USAGE_H

#include "../mutex.h"
#include "../types/fixed_array.h"
#include "../../common_includes/syscalls/process.h"

typedef uint64_t CPUTime;
typedef uint64_t CPUUsageTick;

typedef enum CPUUsageMode
{
    CPU_USAGE_PAUSED,
    CPU_USAGE_USER,
    CPU_USAGE_SYSTEM,
} CPUUsageMode;

typedef struct CPUUsageBucket
{
    CPUUsageTick tick;
    CPUTime usec;
} CPUUsageBucket;

typedef CPUUsageBucket * CPUUsageBucketPtr;
OptionalDef(CPUUsageBucket);

bool CPUUsageBucket__equals(CPUUsageBucketPtr self, CPUUsageBucketPtr other);
FixedArrayDefine(CPUUsageBucket, PROCESS_CPU_LONG_TICKS + 1, CPUUsageBuckets, (EqualFunction)CPUUsageBucket__equals);
typedef CPUUsageBuckets * CPUUsageBucketsPtr;

typedef struct CPUUsage
{
    mutex_t lock;
    CPUTime active_usec;
    CPUTime scheduled_usec;
    CPUTime started_at;
    CPUTime observed_user_wait_usec;
    CPUTime user_usec;
    CPUTime system_usec;
    CPUUsageMode mode;
    bool running;
    CPUUsageBuckets active_buckets;
    CPUUsageBuckets scheduled_buckets;
} CPUUsage;

typedef CPUUsage * CPUUsagePtr;

CPUUsage CPUUsage__new(void);
void CPUUsage__start(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec);
void CPUUsage__start_mode(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec, CPUUsageMode mode);
void CPUUsage__transition(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec, CPUUsageMode mode);
void CPUUsage__stop(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec);
ProcessCPUUsage CPUUsage__snapshot(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec);

#endif
