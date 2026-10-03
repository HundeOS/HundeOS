#ifndef HUND_COMMON_TIME_CLOCK_PAGE_H
#define HUND_COMMON_TIME_CLOCK_PAGE_H

#include <stdint.h>

#include "../types/optional.h"

#define CLOCK_PAGE_VADDR 0x72000000ULL
#define CLOCK_PAGE_SIZE 4096ULL
#define CLOCK_PAGE_MAGIC 0x48554e44434c4f43ULL
#define CLOCK_PAGE_VERSION 2ULL

typedef struct RealtimeClockSample
{
    uint64_t unix_usec;
    uint64_t monotonic_usec;
} RealtimeClockSample;

OptionalDef(RealtimeClockSample);
typedef const Optional(RealtimeClockSample) * ConstOptionalRealtimeClockSamplePtr;

static inline RealtimeClockSample RealtimeClockSample__new(uint64_t unix_usec, uint64_t monotonic_usec)
{
    return (RealtimeClockSample){.unix_usec = unix_usec, .monotonic_usec = monotonic_usec};
}

typedef struct ClockPage
{
    uint64_t sequence;
    uint64_t magic;
    uint64_t version;
    uint64_t timer_frequency;
    uint64_t realtime_available;
    uint64_t realtime_unix_usec;
    uint64_t realtime_monotonic_usec;
} ClockPage;

typedef ClockPage * ClockPagePtr;
typedef const ClockPage * ConstClockPagePtr;

typedef struct ClockSnapshot
{
    uint64_t timer_frequency;
    Optional(RealtimeClockSample) realtime_clock;
} ClockSnapshot;

OptionalDef(ClockSnapshot);

static inline ClockSnapshot ClockSnapshot__new(uint64_t timer_frequency,
                                               ConstOptionalRealtimeClockSamplePtr realtime_clock)
{
    return (ClockSnapshot){.timer_frequency = timer_frequency, .realtime_clock = *realtime_clock};
}

void ClockPage__publish(ClockPagePtr self, uint64_t timer_frequency,
                        ConstOptionalRealtimeClockSamplePtr realtime_clock);
Optional(ClockSnapshot) ClockPage__snapshot(ConstClockPagePtr self);
ConstClockPagePtr ClockPage__shared(void);

#endif
