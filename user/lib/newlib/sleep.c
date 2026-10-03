#include <unistd.h>

#include "sys/process_sleep.h"
#include "sys/process.h"

unsigned int sleep(unsigned int seconds)
{
    if (seconds > 0)
    {
        sleep_usec((uint64_t)seconds * 1000000ULL);
    }
    return 0;
}

unsigned int alarm(unsigned int seconds)
{
    ProcessSignalControlRequest request = {
        .operation = PROCESS_SIGNAL_CONTROL_ALARM,
        .alarm_seconds = seconds,
    };
    if (process_signal_control(&request) != PROCESS_SIGNAL_OK)
    {
        return 0;
    }
    return (unsigned int)request.previous_alarm_seconds;
}
