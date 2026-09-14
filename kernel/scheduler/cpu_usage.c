#include "cpu_usage.h"
#include "../arch/cpu.h"

FixedArrayBodyDefine(CPUUsageBuckets);

bool CPUUsageBucket__equals(CPUUsageBucketPtr self, CPUUsageBucketPtr other)
{
    return self->tick == other->tick && self->usec == other->usec;
}

static CPUUsageBucket CPUUsageBucket__new(CPUUsageTick tick)
{
    return (CPUUsageBucket){.tick = tick, .usec = 0};
}

CPUUsage CPUUsage__new(void)
{
    return (CPUUsage){.lock = MUTEX_INIT, .buckets = CPUUsageBuckets__new()};
}

static void CPUUsage__update(CPUUsagePtr self, CPUTime now)
{
    if (!self->running || now <= self->started_at)
    {
        return;
    }
    CPUTime start = self->started_at;
    self->total_usec += now - start;
    self->started_at = now;

    // Only retain the last long window plus the incomplete current tick.
    CPUUsageTick end_tick = now / PROCESS_CPU_TICK_USEC;
    CPUTime oldest =
        end_tick > PROCESS_CPU_LONG_TICKS ? (end_tick - PROCESS_CPU_LONG_TICKS) * PROCESS_CPU_TICK_USEC : 0;
    if (start < oldest)
    {
        start = oldest;
    }
    while (start < now)
    {
        CPUUsageTick tick = start / PROCESS_CPU_TICK_USEC;
        CPUTime end = (tick + 1) * PROCESS_CPU_TICK_USEC;
        if (end > now)
        {
            end = now;
        }
        ArrayIndex index = (ArrayIndex)(tick % (PROCESS_CPU_LONG_TICKS + 1));
        CPUUsageBucket bucket = unwrap_optional(CPUUsageBuckets__at(&self->buckets, index), CPUUsageBucket__new(tick));
        if (bucket.tick != tick)
        {
            bucket = CPUUsageBucket__new(tick);
        }
        bucket.usec += end - start;
        match_result(
            CPUUsageBuckets__set(&self->buckets, index, bucket),
            success_do_nothing(),
            error(code, message)
            {
                CPU__halt_all();
                return;
            }
        );
        start = end;
    }
}

void CPUUsage__start(CPUUsagePtr self)
{
    critical_section(self->lock, {
        if (!self->running)
        {
            self->started_at = CPU__time_usec();
            self->running = true;
        }
    });
}

void CPUUsage__stop(CPUUsagePtr self)
{
    critical_section(self->lock, {
        CPUUsage__update(self, CPU__time_usec());
        self->running = false;
    });
}

ProcessCPUUsage CPUUsage__snapshot(CPUUsagePtr self)
{
    ProcessCPUUsage usage = ProcessCPUUsage__new();
    critical_section(self->lock, {
        CPUTime now = CPU__time_usec();
        CPUUsage__update(self, now);
        usage.total_usec = self->total_usec;
        CPUUsageTick tick = now / PROCESS_CPU_TICK_USEC;
        CPUTime short_usec = 0;
        CPUTime long_usec = 0;
        for_each_fa(CPUUsageBuckets, self->buckets, index, bucket, {
            if (bucket.tick < tick && tick - bucket.tick <= PROCESS_CPU_LONG_TICKS)
            {
                long_usec += bucket.usec;
                if (tick - bucket.tick <= PROCESS_CPU_SHORT_TICKS)
                {
                    short_usec += bucket.usec;
                }
            }
        });
        usage.short_basis_points = short_usec * 10000 / (PROCESS_CPU_SHORT_TICKS * PROCESS_CPU_TICK_USEC);
        usage.long_basis_points = long_usec * 10000 / (PROCESS_CPU_LONG_TICKS * PROCESS_CPU_TICK_USEC);
    });
    return usage;
}
