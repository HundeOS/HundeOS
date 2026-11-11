#ifndef HUNDE_SYSCALLS_THREAD_WAIT_H
#define HUNDE_SYSCALLS_THREAD_WAIT_H

#include <stdint.h>
#include "calls.h"
#include "types/result.h"

typedef uint32_t ThreadWaitValue;
typedef ThreadWaitValue * ThreadWaitValuePtr;
typedef int64_t ThreadWaitStatus;
typedef int64_t ThreadWakeResult;
typedef uint64_t ThreadWaitTimeout;

#define THREAD_WAIT_INDEFINITE UINT64_MAX

typedef enum ThreadWaitCode
{
    THREAD_WAIT_WOKEN = 0,
    THREAD_WAIT_CHANGED = 1,
    THREAD_WAIT_TIMED_OUT = 2,
    THREAD_WAIT_INVALID = -1,
    THREAD_WAIT_FAILED = -3,
    THREAD_WAIT_INTERRUPTED = SYSCALL_INTERRUPTED,
} ThreadWaitCode;

ResultDef(ThreadWaitStatus);
ResultDef(ThreadWakeResult);

#endif
