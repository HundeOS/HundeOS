#include "ipc/ipc_platform.h"
#include <time.h>
#include <pthread.h>

static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_condition = PTHREAD_COND_INITIALIZER;
static uint64_t event_generation;
static _Thread_local uint64_t observed_generation;

void IpcPlatform__configure_wait(BootWaitMode mode)
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
    event_generation++;
    pthread_cond_broadcast(&event_condition);
    pthread_mutex_unlock(&event_mutex);
}

void IpcPlatform__wait_event(void)
{
    pthread_mutex_lock(&event_mutex);
    if (observed_generation == event_generation)
    {
        pthread_cond_wait(&event_condition, &event_mutex);
    }
    observed_generation = event_generation;
    pthread_mutex_unlock(&event_mutex);
}
