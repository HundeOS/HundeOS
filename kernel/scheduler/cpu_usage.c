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
    return (CPUUsage){
        .lock = MUTEX_INIT, .active_buckets = CPUUsageBuckets__new(), .scheduled_buckets = CPUUsageBuckets__new()};
}

static void CPUUsage__record_bucket(CPUUsageBucketsPtr buckets, CPUUsageTick tick, CPUTime usec)
{
    ArrayIndex index = (ArrayIndex)(tick % (PROCESS_CPU_LONG_TICKS + 1));
    CPUUsageBucket bucket = unwrap_optional(CPUUsageBuckets__at(buckets, index), CPUUsageBucket__new(tick));
    if (bucket.tick != tick)
    {
        bucket = CPUUsageBucket__new(tick);
    }
    bucket.usec += usec;
    match_result(
        CPUUsageBuckets__set(buckets, index, bucket),
        success_do_nothing(),
        error(code, message)
        {
            CPU__halt_all();
        }
    );
}

static void CPUUsage__update(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec)
{
    if (!self->running || now <= self->started_at)
    {
        return;
    }
    CPUTime start = self->started_at;
    CPUTime scheduled_usec = now - start;
    CPUTime user_wait_growth =
        user_wait_usec >= self->observed_user_wait_usec ? user_wait_usec - self->observed_user_wait_usec : 0;
    if (user_wait_growth > scheduled_usec)
    {
        user_wait_growth = scheduled_usec;
    }
    CPUTime active_usec = scheduled_usec - user_wait_growth;
    self->scheduled_usec += scheduled_usec;
    self->active_usec += active_usec;
    self->started_at = now;
    self->observed_user_wait_usec = user_wait_usec;

    // Only retain the last long window plus the incomplete current tick.
    CPUUsageTick end_tick = now / PROCESS_CPU_TICK_USEC;
    CPUTime oldest =
        end_tick > PROCESS_CPU_LONG_TICKS ? (end_tick - PROCESS_CPU_LONG_TICKS) * PROCESS_CPU_TICK_USEC : 0;
    if (start < oldest)
    {
        CPUTime retained_usec = now - oldest;
        active_usec = active_usec * retained_usec / scheduled_usec;
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
        CPUTime interval_usec = now - start;
        CPUTime bucket_usec = end - start;
        CPUTime bucket_active_usec =
            interval_usec == bucket_usec ? active_usec : active_usec * bucket_usec / interval_usec;
        CPUUsage__record_bucket(&self->scheduled_buckets, tick, bucket_usec);
        CPUUsage__record_bucket(&self->active_buckets, tick, bucket_active_usec);
        active_usec -= bucket_active_usec;
        start = end;
    }
}

void CPUUsage__start(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec)
{
    critical_section(
        self->lock,
        {
            if (!self->running)
            {
                self->started_at = now;
                self->observed_user_wait_usec = user_wait_usec;
                self->running = true;
            }
        }
    );
}

void CPUUsage__stop(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec)
{
    critical_section(
        self->lock,
        {
            CPUUsage__update(self, now, user_wait_usec);
            self->running = false;
        }
    );
}

typedef struct CPUUsageWindow
{
    CPUTime short_usec;
    CPUTime long_usec;
} CPUUsageWindow;

static CPUUsageWindow CPUUsageWindow__new(void)
{
    return (CPUUsageWindow){.short_usec = 0, .long_usec = 0};
}

static CPUUsageWindow CPUUsageBuckets__window(CPUUsageBucketsPtr buckets, CPUUsageTick tick)
{
    CPUUsageWindow window = CPUUsageWindow__new();
    for_each_fa_ptr(
        CPUUsageBuckets,
        buckets,
        index,
        bucket,
        {
            if (bucket.tick < tick && tick - bucket.tick <= PROCESS_CPU_LONG_TICKS)
            {
                window.long_usec += bucket.usec;
                if (tick - bucket.tick <= PROCESS_CPU_SHORT_TICKS)
                {
                    window.short_usec += bucket.usec;
                }
            }
        }
    );
    return window;
}

ProcessCPUUsage CPUUsage__snapshot(CPUUsagePtr self, CPUTime now, CPUTime user_wait_usec)
{
    ProcessCPUUsage usage = ProcessCPUUsage__new();
    critical_section(
        self->lock,
        {
            CPUUsage__update(self, now, user_wait_usec);
            usage.total_usec = self->active_usec;
            usage.scheduled_usec = self->scheduled_usec;
            CPUUsageTick tick = now / PROCESS_CPU_TICK_USEC;
            CPUUsageWindow active = CPUUsageBuckets__window(&self->active_buckets, tick);
            CPUUsageWindow scheduled = CPUUsageBuckets__window(&self->scheduled_buckets, tick);
            usage.short_basis_points =
                active.short_usec * 10000 / (PROCESS_CPU_SHORT_TICKS * PROCESS_CPU_TICK_USEC);
            usage.long_basis_points =
                active.long_usec * 10000 / (PROCESS_CPU_LONG_TICKS * PROCESS_CPU_TICK_USEC);
            usage.scheduled_short_basis_points =
                scheduled.short_usec * 10000 / (PROCESS_CPU_SHORT_TICKS * PROCESS_CPU_TICK_USEC);
            usage.scheduled_long_basis_points =
                scheduled.long_usec * 10000 / (PROCESS_CPU_LONG_TICKS * PROCESS_CPU_TICK_USEC);
        }
    );

    return usage;
}
