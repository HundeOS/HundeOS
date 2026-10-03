#include <errno.h>
#include <sys/times.h>

#include "sys/process.h"

clock_t times(struct tms *buffer)
{
    if (buffer == 0)
    {
        errno = EFAULT;
        return (clock_t)-1;
    }

    ProcessTimes process_time = ProcessTimes__new();
    if (process_times(&process_time) != PROCESS_TIMES_OK)
    {
        errno = EINVAL;
        return (clock_t)-1;
    }

    buffer->tms_utime = (clock_t)process_time.user_usec;
    buffer->tms_stime = (clock_t)process_time.system_usec;
    buffer->tms_cutime = (clock_t)process_time.children_user_usec;
    buffer->tms_cstime = (clock_t)process_time.children_system_usec;
    return (clock_t)process_time.elapsed_usec;
}
