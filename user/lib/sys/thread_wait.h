#ifndef HUNDE_SYS_THREAD_WAIT_H
#define HUNDE_SYS_THREAD_WAIT_H

#include <stdatomic.h>
#include "syscalls/thread_wait.h"

typedef _Atomic(ThreadWaitValue) ThreadWaitWord;
typedef ThreadWaitWord * ThreadWaitWordPtr;

_Static_assert(sizeof(ThreadWaitWord) == sizeof(ThreadWaitValue), "Atomic wait word ABI size");
_Static_assert(_Alignof(ThreadWaitWord) == _Alignof(ThreadWaitValue), "Atomic wait word ABI alignment");

ThreadWaitStatus thread_wait(ThreadWaitWordPtr word, ThreadWaitValue expected, ThreadWaitTimeout timeout_usec);
ThreadWakeResult thread_wake(ThreadWaitWordPtr word, uint64_t count);

#endif
