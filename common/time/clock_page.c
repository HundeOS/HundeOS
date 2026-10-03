#include "clock_page.h"

#define CLOCK_PAGE_SNAPSHOT_ATTEMPTS 4

void ClockPage__publish(ClockPagePtr self, uint64_t timer_frequency, ConstOptionalRealtimeClockSamplePtr realtime_clock)
{
    uint64_t sequence = __atomic_load_n(&self->sequence, __ATOMIC_RELAXED);
    if ((sequence & 1ULL) != 0)
    {
        sequence++;
    }

    __atomic_store_n(&self->sequence, sequence + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&self->magic, CLOCK_PAGE_MAGIC, __ATOMIC_RELAXED);
    __atomic_store_n(&self->version, CLOCK_PAGE_VERSION, __ATOMIC_RELAXED);
    __atomic_store_n(&self->timer_frequency, timer_frequency, __ATOMIC_RELAXED);
    match_optional(
        *realtime_clock,
        some(sample)
        {
            __atomic_store_n(&self->realtime_unix_usec, sample.unix_usec, __ATOMIC_RELAXED);
            __atomic_store_n(&self->realtime_monotonic_usec, sample.monotonic_usec, __ATOMIC_RELAXED);
            __atomic_store_n(&self->realtime_available, 1, __ATOMIC_RELAXED);
        },
        none()
        {
            __atomic_store_n(&self->realtime_available, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&self->realtime_unix_usec, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&self->realtime_monotonic_usec, 0, __ATOMIC_RELAXED);
        }
    );
    __atomic_store_n(&self->sequence, sequence + 2, __ATOMIC_RELEASE);
}

Optional(ClockSnapshot) ClockPage__snapshot(ConstClockPagePtr self)
{
    for (uint64_t attempt = 0; attempt < CLOCK_PAGE_SNAPSHOT_ATTEMPTS; attempt++)
    {
        uint64_t before = __atomic_load_n(&self->sequence, __ATOMIC_ACQUIRE);
        if ((before & 1ULL) != 0)
        {
            continue;
        }

        uint64_t magic = __atomic_load_n(&self->magic, __ATOMIC_RELAXED);
        uint64_t version = __atomic_load_n(&self->version, __ATOMIC_RELAXED);
        uint64_t timer_frequency = __atomic_load_n(&self->timer_frequency, __ATOMIC_RELAXED);
        uint64_t realtime_available = __atomic_load_n(&self->realtime_available, __ATOMIC_RELAXED);
        uint64_t realtime_unix_usec = __atomic_load_n(&self->realtime_unix_usec, __ATOMIC_RELAXED);
        uint64_t realtime_monotonic_usec = __atomic_load_n(&self->realtime_monotonic_usec, __ATOMIC_RELAXED);
        uint64_t after = __atomic_load_n(&self->sequence, __ATOMIC_ACQUIRE);

        if (before == after && (after & 1ULL) == 0 && magic == CLOCK_PAGE_MAGIC && version == CLOCK_PAGE_VERSION)
        {
            Optional(RealtimeClockSample) realtime_clock =
                realtime_available == 0
                    ? None(RealtimeClockSample)
                    : Some(RealtimeClockSample, RealtimeClockSample__new(realtime_unix_usec, realtime_monotonic_usec));
            return Some(ClockSnapshot, ClockSnapshot__new(timer_frequency, &realtime_clock));
        }
    }

    return None(ClockSnapshot);
}

ConstClockPagePtr ClockPage__shared(void)
{
    return (ConstClockPagePtr)CLOCK_PAGE_VADDR;
}
