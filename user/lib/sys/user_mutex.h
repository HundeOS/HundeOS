#ifndef HUNDE_SYS_USER_MUTEX_H
#define HUNDE_SYS_USER_MUTEX_H

#include <stdbool.h>
#include "thread_wait.h"

typedef struct UserMutex
{
    ThreadWaitWord state;
} UserMutex;

typedef UserMutex * UserMutexPtr;

UserMutex UserMutex__new(void);
bool UserMutex__try_lock(UserMutexPtr self);
ThreadWaitStatus UserMutex__lock(UserMutexPtr self);
ThreadWaitStatus UserMutex__unlock(UserMutexPtr self);

#endif
