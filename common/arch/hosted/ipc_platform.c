#include "ipc/ipc_platform.h"
#include <time.h>
#include <pthread.h>

static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_condition = PTHREAD_COND_INITIALIZER;
static IpcDoorbell local_doorbell;

IpcDoorbellPtr IpcPlatform__local_doorbell(void)
{
    return &local_doorbell;
}

void IpcPlatform__configure_wait(BootWaitMode mode)
{
}

void IpcPlatform__configure_timer_frequency(uint64_t frequency)
{
}

uint64_t IpcPlatform__monotonic_usec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL;
}

void IpcPlatform__relax(void)
{
}

void IpcPlatform__signal_event(void)
{
    pthread_mutex_lock(&event_mutex);
    IpcEvent__ring(IpcDoorbell__mutable_event(&local_doorbell));
    pthread_cond_broadcast(&event_condition);
    pthread_mutex_unlock(&event_mutex);
}

IpcEventGeneration IpcPlatform__prepare_wait(ConstIpcEventPtr event)
{
    return IpcEvent__generation(event);
}

void IpcPlatform__wait_event(ConstIpcEventPtr event, IpcEventGeneration observed)
{
    pthread_mutex_lock(&event_mutex);
    if (!IpcEvent__changed(event, observed))
    {
        pthread_cond_wait(&event_condition, &event_mutex);
    }
    pthread_mutex_unlock(&event_mutex);
}
