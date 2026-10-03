#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "../../../common/ipc/ipc_platform.h"
#include "../../../common/time/clock_page.h"

typedef void (*ClockConfigureFrequency)(uint64_t frequency);
typedef uint64_t (*ClockReadMonotonicUsec)(void);

int Clock__gettime_from(ConstClockPagePtr page, clockid_t clock_id, struct timespec * value,
                        ClockConfigureFrequency configure_frequency, ClockReadMonotonicUsec read_monotonic_usec)
{
    if (value == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (clock_id != CLOCK_MONOTONIC)
    {
        errno = EINVAL;
        return -1;
    }

    match_optional(
        ClockPage__snapshot(page),
        some(snapshot)
        {
            configure_frequency(snapshot.timer_frequency);
            uint64_t usec = read_monotonic_usec();
            value->tv_sec = (time_t)(usec / 1000000ULL);
            value->tv_nsec = (long)((usec % 1000000ULL) * 1000ULL);
            return 0;
        },
        none()
        {
            errno = EINVAL;
            return -1;
        }
    );
}

int clock_gettime(clockid_t clock_id, struct timespec * value)
{
    return Clock__gettime_from(ClockPage__shared(), clock_id, value, IpcPlatform__configure_timer_frequency,
                               IpcPlatform__monotonic_usec);
}

int Clock__gettimeofday_from(ConstClockPagePtr page, struct timeval * value,
                             ClockConfigureFrequency configure_frequency,
                             ClockReadMonotonicUsec read_monotonic_usec)
{
    if (value == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    value->tv_sec = 0;
    value->tv_usec = 0;
    match_optional(
        ClockPage__snapshot(page),
        some(snapshot)
        {
            configure_frequency(snapshot.timer_frequency);
            match_optional(
                snapshot.realtime_clock,
                some(sample)
                {
                    uint64_t monotonic_usec = read_monotonic_usec();
                    if (monotonic_usec < sample.monotonic_usec)
                    {
                        errno = EINVAL;
                        return -1;
                    }
                    uint64_t realtime_usec = sample.unix_usec + monotonic_usec - sample.monotonic_usec;
                    value->tv_sec = (time_t)(realtime_usec / 1000000ULL);
                    value->tv_usec = (suseconds_t)(realtime_usec % 1000000ULL);
                    return 0;
                },
                none()
                {
                    errno = ENOSYS;
                    return -1;
                }
            );
        },
        none()
        {
            errno = EINVAL;
            return -1;
        }
    );
}

int gettimeofday(struct timeval * value, void * timezone)
{
    return Clock__gettimeofday_from(ClockPage__shared(), value, IpcPlatform__configure_timer_frequency,
                                    IpcPlatform__monotonic_usec);
}
