#include "user_mutex.h"

typedef enum UserMutexState
{
    USER_MUTEX_UNLOCKED = 0,
    USER_MUTEX_LOCKED = 1,
    USER_MUTEX_CONTENDED = 2,
} UserMutexState;

UserMutex UserMutex__new(void)
{
    return (UserMutex){.state = USER_MUTEX_UNLOCKED};
}

bool UserMutex__try_lock(UserMutexPtr self)
{
    ThreadWaitValue expected = USER_MUTEX_UNLOCKED;
    return atomic_compare_exchange_strong_explicit(&self->state, &expected, USER_MUTEX_LOCKED, memory_order_acquire,
                                                   memory_order_relaxed);
}

ThreadWaitStatus UserMutex__lock(UserMutexPtr self)
{
    if (UserMutex__try_lock(self))
    {
        return THREAD_WAIT_WOKEN;
    }
    while (atomic_exchange_explicit(&self->state, USER_MUTEX_CONTENDED, memory_order_acquire) != USER_MUTEX_UNLOCKED)
    {
        ThreadWaitStatus status = thread_wait(&self->state, USER_MUTEX_CONTENDED, THREAD_WAIT_INDEFINITE);
        if (status != THREAD_WAIT_WOKEN && status != THREAD_WAIT_CHANGED && status != THREAD_WAIT_INTERRUPTED)
        {
            return status;
        }
    }
    return THREAD_WAIT_WOKEN;
}

ThreadWaitStatus UserMutex__unlock(UserMutexPtr self)
{
    ThreadWaitValue previous = atomic_exchange_explicit(&self->state, USER_MUTEX_UNLOCKED, memory_order_release);
    if (previous == USER_MUTEX_UNLOCKED)
    {
        return THREAD_WAIT_INVALID;
    }
    if (previous == USER_MUTEX_CONTENDED)
    {
        ThreadWakeResult result = thread_wake(&self->state, 1);
        if (result < 0)
        {
            return result;
        }
    }
    return THREAD_WAIT_WOKEN;
}
